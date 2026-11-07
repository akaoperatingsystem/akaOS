#include <xscore/ahci.h>
#include <xscore/pci.h>
#include <xscore/serial.h>
#include <xscore/vmm.h>
#include <shim/linux/ata/ahci.h>

#define MAX_AHCI_PORTS 32

struct ahci_controller {
    uintptr_t bar_vaddr;
    uint64_t bar_paddr;
    uint64_t bar_size;
    uint32_t ports_impl;
    uint32_t cap;
    int present;
};

static struct ahci_controller g_ahci;

int xscore_ahci_initialize(void) {
    uint32_t device_count = xscore_pci_device_count();
    for (uint32_t i = 0; i < device_count; i++) {
        const struct xscore_pci_device *pci_dev = xscore_pci_get_device(i);
        /* PCI Class 0x01 (Mass Storage), Subclass 0x06 (SATA Controller), ProgIF 0x01 (AHCI) */
        if (pci_dev->class_code == 0x01 && pci_dev->subclass == 0x06 && pci_dev->prog_if == 0x01) {
            xscore_serial_write("[INFO] Linux-ported AHCI SATA Storage Controller detected! Probing...\n");

            if (pci_dev->bar_is_io[5] || !pci_dev->bars[5]) {
                xscore_serial_write("[WARN] AHCI BAR5 not valid MMIO\n");
                continue;
            }

            g_ahci.bar_paddr = pci_dev->bars[5];
            g_ahci.bar_size = pci_dev->bar_sizes[5];
            g_ahci.bar_vaddr = (uintptr_t)g_ahci.bar_paddr;

            /* Map MMIO register space */
            uint64_t map_size = (g_ahci.bar_size + 4095) & ~4095ULL;
            if (map_size < 8192) map_size = 8192;
            for (uint64_t offset = 0; offset < map_size; offset += 4096) {
                uint64_t paddr = g_ahci.bar_paddr + offset;
                xscore_vmm_map_page(paddr, paddr, XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
            }

            volatile uint32_t *regs = (volatile uint32_t *)g_ahci.bar_vaddr;
            g_ahci.cap = regs[HOST_CAP / 4];
            g_ahci.ports_impl = regs[HOST_PORTS_IMPL / 4];

            /* Enable AHCI Mode (GHC.AE = bit 31) */
            regs[HOST_CTL / 4] |= (1U << 31);

            g_ahci.present = 1;
            xscore_serial_write("[INFO] AHCI controller online: ports implemented mask = 0x");
            char hex[9];
            uint32_t val = g_ahci.ports_impl;
            for (int h = 7; h >= 0; h--) {
                uint8_t nib = (val >> (h * 4)) & 0xf;
                hex[7 - h] = nib < 10 ? '0' + nib : 'a' + nib - 10;
            }
            hex[8] = '\0';
            xscore_serial_write(hex);
            xscore_serial_write("\n");
            return 1;
        }
    }
    return 0;
}
