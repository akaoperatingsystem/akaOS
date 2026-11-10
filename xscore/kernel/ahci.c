/* Universal AHCI / Serial ATA Host Controller Driver for XSCore
 *
 * Implements:
 * - PCI discovery (Class 01:06:01) and BAR5 (ABAR) MMIO register mapping
 * - Global Host Control (GHC), HBA reset, and port detection/enumeration
 * - FIS architecture (H2D Command FIS, PRDT scatter-gather tables)
 * - Drive identification via ATA_CMD_IDENTIFY (model, serial, LBA48 sectors)
 * - 48-bit LBA DMA Read/Write (ATA_CMD_READ_DMA_EXT, ATA_CMD_WRITE_DMA_EXT)
 * - Hardware cache flushing (ATA_CMD_FLUSH_CACHE_EXT)
 * - Unified storage registration into /dev/sda, /dev/sdb...
 */

#include <xscore/ahci.h>
#include <xscore/storage.h>
#include <xscore/pci.h>
#include <xscore/serial.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <xscore/smp.h>
#include <xscore/timer.h>

void *memset(void *dst, int val, size_t count);
void *memcpy(void *dst, const void *src, size_t count);

#define AHCI_PAGE_SIZE 4096U
#define AHCI_TIMEOUT_SPINS 10000000U
#define AHCI_MMIO_FLAGS (XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE | UINT64_C(0x18))

static volatile struct hba_mem *g_abar = 0;
static uint64_t g_abar_phys = 0;
static uint64_t g_abar_size = 0;
static int g_ahci_present = 0;

static struct xscore_ahci_device g_ahci_devices[AHCI_MAX_PORTS];
static uint32_t g_ahci_device_count = 0;
static xscore_spinlock_t g_ahci_lock = XSCORE_SPINLOCK_INIT;

static inline void ahci_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static void copy_ata_string(char *dst, size_t dst_size, const uint16_t *src, size_t word_count) {
    if (!dst || dst_size == 0) return;
    size_t out_idx = 0;
    for (size_t i = 0; i < word_count && out_idx + 2 < dst_size; ++i) {
        uint16_t w = src[i];
        dst[out_idx++] = (char)(w >> 8);   /* Big-endian byte swap per ATA spec */
        dst[out_idx++] = (char)(w & 0xFF);
    }
    while (out_idx > 0 && (dst[out_idx - 1] == ' ' || dst[out_idx - 1] == 0)) {
        --out_idx;
    }
    dst[out_idx] = '\0';
}

static int ahci_stop_port(volatile struct hba_port *port) {
    port->cmd &= ~HBA_PORT_CMD_ST;
    port->cmd &= ~HBA_PORT_CMD_FRE;

    for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
        if (!(port->cmd & HBA_PORT_CMD_FR) && !(port->cmd & HBA_PORT_CMD_CR)) {
            return 1;
        }
        ahci_cpu_relax();
    }
    return 0;
}

static void ahci_start_port(volatile struct hba_port *port) {
    while (port->cmd & HBA_PORT_CMD_CR) {
        ahci_cpu_relax();
    }
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

static int ahci_find_cmdslot(volatile struct hba_port *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; ++i) {
        if (!(slots & (1U << i))) return i;
    }
    return -1;
}

static int ahci_init_port_buffers(struct xscore_ahci_device *dev, volatile struct hba_port *port) {
    (void)ahci_stop_port(port);

    uint64_t clb_phys = xscore_pmm_allocate_page();
    uint64_t fb_phys = xscore_pmm_allocate_page();
    uint64_t ctba_phys = xscore_pmm_allocate_page();
    if (!clb_phys || !fb_phys || !ctba_phys) {
        return 0;
    }

    dev->clb_phys = clb_phys;
    dev->fb_phys = fb_phys;
    dev->ctba_phys = ctba_phys;

    dev->cmd_headers = (struct hba_cmd_header *)(uintptr_t)clb_phys;
    dev->received_fis = (uint8_t *)(uintptr_t)fb_phys;
    dev->cmd_table = (struct hba_cmd_tbl *)(uintptr_t)ctba_phys;

    memset(dev->cmd_headers, 0, AHCI_PAGE_SIZE);
    memset(dev->received_fis, 0, AHCI_PAGE_SIZE);
    memset(dev->cmd_table, 0, AHCI_PAGE_SIZE);

    port->clb = (uint32_t)clb_phys;
    port->clbu = (uint32_t)(clb_phys >> 32);
    port->fb = (uint32_t)fb_phys;
    port->fbu = (uint32_t)(fb_phys >> 32);

    /* Point command header 0 to command table 0 */
    dev->cmd_headers[0].ctba = (uint32_t)ctba_phys;
    dev->cmd_headers[0].ctbau = (uint32_t)(ctba_phys >> 32);

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    ahci_start_port(port);
    return 1;
}

static int ahci_identify(struct xscore_ahci_device *dev, volatile struct hba_port *port) {
    uintptr_t flags = xscore_spin_lock_irqsave(&g_ahci_lock);
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
        return 0;
    }

    uint64_t bounce_phys = xscore_pmm_allocate_page();
    if (!bounce_phys) {
        xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
        return 0;
    }
    uint8_t *bounce = (uint8_t *)(uintptr_t)bounce_phys;
    memset(bounce, 0, 512);

    struct hba_cmd_header *cmdheader = &dev->cmd_headers[slot];
    cmdheader->cfl = sizeof(struct fis_reg_h2d) / sizeof(uint32_t);
    cmdheader->w = 0;       /* Read operation */
    cmdheader->prdtl = 1;   /* 1 PRDT entry */
    cmdheader->prdbc = 0;
    cmdheader->ctba = (uint32_t)dev->ctba_phys;
    cmdheader->ctbau = (uint32_t)(dev->ctba_phys >> 32);

    struct hba_cmd_tbl *cmdtbl = dev->cmd_table;
    memset(cmdtbl, 0, sizeof(struct hba_cmd_tbl));

    cmdtbl->prdt_entry[0].dba = (uint32_t)bounce_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(bounce_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = 512 - 1; /* 512 bytes */
    cmdtbl->prdt_entry[0].i = 1;

    struct fis_reg_h2d *cmdfis = (struct fis_reg_h2d *)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_IDENTIFY;

    port->is = 0xFFFFFFFF;
    port->ci = (1U << slot);

    int success = 0;
    for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
        if (!(port->ci & (1U << slot))) {
            success = 1;
            break;
        }
        if (port->is & HBA_PORT_IS_TFES) {
            break;
        }
        ahci_cpu_relax();
    }

    if (success) {
        const uint16_t *ident = (const uint16_t *)(const void *)bounce;
        copy_ata_string(dev->serial, sizeof(dev->serial), &ident[10], 10);
        copy_ata_string(dev->firmware, sizeof(dev->firmware), &ident[23], 4);
        copy_ata_string(dev->model, sizeof(dev->model), &ident[27], 20);

        uint64_t sectors = ((uint64_t)ident[103] << 48) |
                          ((uint64_t)ident[102] << 32) |
                          ((uint64_t)ident[101] << 16) |
                          ((uint64_t)ident[100]);
        if (sectors == 0) {
            sectors = ((uint32_t)ident[61] << 16) | ident[60];
        }
        dev->sector_count = sectors;
        dev->sector_size = 512;
        dev->active = 1;
    }

    (void)xscore_pmm_free_page(bounce_phys);
    xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
    return success;
}

int xscore_ahci_initialize(void) {
    uint32_t pci_count = xscore_pci_device_count();
    const struct xscore_pci_device *ahci_dev = 0;

    for (uint32_t i = 0; i < pci_count; ++i) {
        const struct xscore_pci_device *dev = xscore_pci_get_device(i);
        if (dev && dev->class_code == 0x01 && dev->subclass == 0x06 && dev->prog_if == 0x01) {
            ahci_dev = dev;
            break;
        }
    }

    if (!ahci_dev) {
        return 0;
    }

    xscore_serial_write("[AHCI] SATA AHCI 1.x/2.x/3.x Controller discovered on PCI\n");

    if (!xscore_pci_enable_bus_master(ahci_dev)) {
        xscore_serial_write("[AHCI] Failed to enable PCI bus mastering and memory space\n");
        return 0;
    }

    /* BAR5 contains ABAR (AHCI Base Address Register) */
    uint64_t abar_phys = ahci_dev->bars[5];
    uint64_t abar_size = ahci_dev->bar_sizes[5];
    if (!abar_phys || ahci_dev->bar_is_io[5] || abar_size < AHCI_PAGE_SIZE) {
        xscore_serial_write("[AHCI] Invalid ABAR (BAR5) address or size\n");
        return 0;
    }

    g_abar_phys = abar_phys;
    g_abar_size = (abar_size + AHCI_PAGE_SIZE - 1) & ~(uint64_t)(AHCI_PAGE_SIZE - 1);

    for (uint64_t off = 0; off < g_abar_size; off += AHCI_PAGE_SIZE) {
        uint64_t p = g_abar_phys + off;
        if (!xscore_vmm_is_mapped(p, 0)) {
            (void)xscore_vmm_map_page(p, p, AHCI_MMIO_FLAGS);
        }
    }

    g_abar = (volatile struct hba_mem *)(uintptr_t)g_abar_phys;

    /* Global Host Control: AHCI Enable */
    g_abar->ghc |= HBA_GHC_AE;

    /* Perform Controller Reset */
    g_abar->ghc |= HBA_GHC_HR;
    for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
        if (!(g_abar->ghc & HBA_GHC_HR)) break;
        ahci_cpu_relax();
    }
    g_abar->ghc |= HBA_GHC_AE;

    /* BIOS/OS Handoff if supported (BOHC) */
    if (g_abar->cap2 & (1U << 0)) {
        g_abar->bohc |= (1U << 1); /* OOS: OS Ownership Set */
        for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
            if (!(g_abar->bohc & (1U << 0))) break; /* Wait for BOS to clear */
            ahci_cpu_relax();
        }
    }

    g_ahci_present = 1;
    g_ahci_device_count = 0;
    memset(g_ahci_devices, 0, sizeof(g_ahci_devices));

    uint32_t pi = g_abar->pi;
    for (uint8_t p = 0; p < 32; ++p) {
        if (!(pi & (1U << p))) continue;

        volatile struct hba_port *port = &g_abar->ports[p];
        uint32_t ssts = port->ssts;
        uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
        uint8_t det = (uint8_t)(ssts & 0x0F);

        /* DET=3: Device detected and Phy communication established */
        /* IPM=1: Device in active power state */
        if (det != 3 || ipm != 1) continue;

        uint32_t sig = port->sig;
        if (sig != SATA_SIG_ATA && sig != SATA_SIG_ATAPI) continue;

        struct xscore_ahci_device *dev = &g_ahci_devices[g_ahci_device_count];
        dev->port_num = p;
        dev->atapi = (sig == SATA_SIG_ATAPI);

        if (!ahci_init_port_buffers(dev, port)) {
            continue;
        }

        if (sig == SATA_SIG_ATA) {
            if (ahci_identify(dev, port)) {
                ++g_ahci_device_count;

                xscore_serial_write("[AHCI] SATA Drive ready: Port ");
                char pbuf[16];
                pbuf[0] = (char)('0' + p); pbuf[1] = '\0';
                xscore_serial_write(pbuf);
                xscore_serial_write(" Model: ");
                xscore_serial_write(dev->model[0] ? dev->model : "Generic SATA Drive");
                xscore_serial_write("\n");

                /* Register into unified storage subsystem */
                struct xscore_storage_device sdev;
                memset(&sdev, 0, sizeof(sdev));
                sdev.type = XSCORE_STORAGE_AHCI;
                sdev.active = 1;
                sdev.port_or_target = p;
                sdev.sector_size = dev->sector_size;
                sdev.total_sectors = dev->sector_count;
                memcpy(sdev.model, dev->model, sizeof(sdev.model));
                memcpy(sdev.serial, dev->serial, sizeof(sdev.serial));
                memcpy(sdev.firmware, dev->firmware, sizeof(sdev.firmware));

                /* Assign canonical device name /dev/sda, /dev/sdb... */
                char name[32] = "/dev/sda";
                name[7] = (char)('a' + (g_ahci_device_count - 1));
                memcpy(sdev.name, name, sizeof(sdev.name));

                (void)xscore_storage_register_device(&sdev);
            }
        }
    }

    return g_ahci_device_count > 0;
}

int xscore_ahci_present(void) {
    return g_ahci_present && g_ahci_device_count > 0;
}

uint32_t xscore_ahci_device_count(void) {
    return g_ahci_device_count;
}

const struct xscore_ahci_device *xscore_ahci_get_device(uint32_t index) {
    if (index < g_ahci_device_count) return &g_ahci_devices[index];
    return 0;
}

static int ahci_transfer(uint8_t port_num, uint64_t start_lba, uint32_t count, void *buffer, int write) {
    if (!g_ahci_present || !buffer || count == 0) return 0;
    struct xscore_ahci_device *dev = 0;
    for (uint32_t i = 0; i < g_ahci_device_count; ++i) {
        if (g_ahci_devices[i].port_num == port_num) {
            dev = &g_ahci_devices[i];
            break;
        }
    }
    if (!dev || !dev->active) return 0;

    volatile struct hba_port *port = &g_abar->ports[port_num];

    uintptr_t flags = xscore_spin_lock_irqsave(&g_ahci_lock);
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
        return 0;
    }

    uint64_t bounce_phys = xscore_pmm_allocate_page();
    if (!bounce_phys) {
        xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
        return 0;
    }
    uint8_t *bounce = (uint8_t *)(uintptr_t)bounce_phys;

    uint32_t sectors_transferred = 0;
    int success = 1;

    while (sectors_transferred < count) {
        uint32_t batch = count - sectors_transferred;
        if (batch > 8) batch = 8; /* 8 sectors (4KB page) */

        size_t byte_count = (size_t)batch * 512U;
        uint64_t lba = start_lba + sectors_transferred;

        if (write) {
            memcpy(bounce, (const uint8_t *)buffer + (size_t)sectors_transferred * 512U, byte_count);
        }

        struct hba_cmd_header *cmdheader = &dev->cmd_headers[slot];
        cmdheader->cfl = sizeof(struct fis_reg_h2d) / sizeof(uint32_t);
        cmdheader->w = write ? 1 : 0;
        cmdheader->prdtl = 1;
        cmdheader->prdbc = 0;
        cmdheader->ctba = (uint32_t)dev->ctba_phys;
        cmdheader->ctbau = (uint32_t)(dev->ctba_phys >> 32);

        struct hba_cmd_tbl *cmdtbl = dev->cmd_table;
        memset(cmdtbl, 0, sizeof(struct hba_cmd_tbl));

        cmdtbl->prdt_entry[0].dba = (uint32_t)bounce_phys;
        cmdtbl->prdt_entry[0].dbau = (uint32_t)(bounce_phys >> 32);
        cmdtbl->prdt_entry[0].dbc = (uint32_t)(byte_count - 1);
        cmdtbl->prdt_entry[0].i = 1;

        struct fis_reg_h2d *cmdfis = (struct fis_reg_h2d *)(&cmdtbl->cfis);
        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c = 1;
        cmdfis->command = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;

        cmdfis->lba0 = (uint8_t)lba;
        cmdfis->lba1 = (uint8_t)(lba >> 8);
        cmdfis->lba2 = (uint8_t)(lba >> 16);
        cmdfis->device = 1 << 6; /* LBA mode */

        cmdfis->lba3 = (uint8_t)(lba >> 24);
        cmdfis->lba4 = (uint8_t)(lba >> 32);
        cmdfis->lba5 = (uint8_t)(lba >> 40);

        cmdfis->countl = (uint8_t)(batch & 0xFF);
        cmdfis->counth = (uint8_t)((batch >> 8) & 0xFF);

        port->is = 0xFFFFFFFF;
        port->ci = (1U << slot);

        int batch_ok = 0;
        for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
            if (!(port->ci & (1U << slot))) {
                batch_ok = 1;
                break;
            }
            if (port->is & HBA_PORT_IS_TFES) break;
            ahci_cpu_relax();
        }

        if (!batch_ok) {
            success = 0;
            break;
        }

        if (!write) {
            memcpy((uint8_t *)buffer + (size_t)sectors_transferred * 512U, bounce, byte_count);
        }

        sectors_transferred += batch;
    }

    (void)xscore_pmm_free_page(bounce_phys);
    xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
    return success;
}

int xscore_ahci_read_sectors(uint8_t port_num, uint64_t start_lba, uint32_t count, void *buffer) {
    return ahci_transfer(port_num, start_lba, count, buffer, 0);
}

int xscore_ahci_write_sectors(uint8_t port_num, uint64_t start_lba, uint32_t count, const void *buffer) {
    return ahci_transfer(port_num, start_lba, count, (void *)buffer, 1);
}

int xscore_ahci_flush(uint8_t port_num) {
    if (!g_ahci_present) return 0;
    struct xscore_ahci_device *dev = 0;
    for (uint32_t i = 0; i < g_ahci_device_count; ++i) {
        if (g_ahci_devices[i].port_num == port_num) {
            dev = &g_ahci_devices[i];
            break;
        }
    }
    if (!dev || !dev->active) return 0;

    volatile struct hba_port *port = &g_abar->ports[port_num];

    uintptr_t flags = xscore_spin_lock_irqsave(&g_ahci_lock);
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
        return 0;
    }

    struct hba_cmd_header *cmdheader = &dev->cmd_headers[slot];
    cmdheader->cfl = sizeof(struct fis_reg_h2d) / sizeof(uint32_t);
    cmdheader->w = 1;
    cmdheader->prdtl = 0;
    cmdheader->prdbc = 0;
    cmdheader->ctba = (uint32_t)dev->ctba_phys;
    cmdheader->ctbau = (uint32_t)(dev->ctba_phys >> 32);

    struct hba_cmd_tbl *cmdtbl = dev->cmd_table;
    memset(cmdtbl, 0, sizeof(struct hba_cmd_tbl));

    struct fis_reg_h2d *cmdfis = (struct fis_reg_h2d *)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_FLUSH_CACHE_EXT;
    cmdfis->device = 1 << 6;

    port->is = 0xFFFFFFFF;
    port->ci = (1U << slot);

    int success = 0;
    for (uint32_t spin = 0; spin < AHCI_TIMEOUT_SPINS; ++spin) {
        if (!(port->ci & (1U << slot))) {
            success = 1;
            break;
        }
        if (port->is & HBA_PORT_IS_TFES) break;
        ahci_cpu_relax();
    }

    xscore_spin_unlock_irqrestore(&g_ahci_lock, flags);
    return success;
}
