/* Universal Storage Subsystem & Partition Manager for XSCore
 *
 * Implements:
 * - Unified storage device registry (AHCI SATA, NVMe, VirtIO-Block, IDE/ATA, USB)
 * - MBR (Master Boot Record) partition table parser
 * - GPT (GUID Partition Table) parser
 * - Canonical /dev node population (/dev/sda, /dev/sda1..., /dev/nvme0n1, /dev/nvme0n1p1...)
 * - Unified block I/O dispatch with automatic partition offset translation
 */

#include <xscore/storage.h>
#include <xscore/ahci.h>
#include <xscore/nvme.h>
#include <xscore/virtio_blk.h>
#include <xscore/vfs.h>
#include <xscore/serial.h>
#include <xscore/memory.h>
#include <xscore/smp.h>

void *memset(void *dst, int val, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
int memcmp(const void *s1, const void *s2, size_t n);

static struct xscore_storage_device g_storage_devices[XSCORE_STORAGE_MAX_DEVICES];
static uint32_t g_storage_device_count = 0;
static xscore_spinlock_t g_storage_lock = XSCORE_SPINLOCK_INIT;
static int g_storage_initialized = 0;

static void k_strncpy(char *dst, const char *src, size_t max) {
    if (!dst || max == 0) return;
    size_t i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int k_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int xscore_partition_parse_mbr(struct xscore_storage_device *dev, const uint8_t *sector0) {
    if (!dev || !sector0) return 0;
    /* Check 0x55AA boot signature at byte 510-511 */
    if (sector0[510] != 0x55 || sector0[511] != 0xAA) {
        return 0;
    }

    dev->partition_count = 0;
    const struct mbr_partition_record *records =
        (const struct mbr_partition_record *)(sector0 + 0x1BE);

    for (uint32_t i = 0; i < 4; ++i) {
        const struct mbr_partition_record *rec = &records[i];
        if (rec->os_type == 0 || rec->size_in_lba == 0) continue;

        /* Skip protective MBR for GPT (0xEE) here; handled in GPT parser */
        if (rec->os_type == 0xEE) continue;

        struct xscore_partition *part = &dev->partitions[dev->partition_count];
        memset(part, 0, sizeof(*part));

        part->start_lba = rec->starting_lba;
        part->sector_count = rec->size_in_lba;
        part->sector_size = dev->sector_size ? dev->sector_size : 512;
        part->type_mbr = rec->os_type;
        part->bootable = (rec->boot_indicator & 0x80) ? 1 : 0;
        part->active = 1;
        part->is_gpt = 0;

        /* Generate partition name, e.g. /dev/sda1 or /dev/nvme0n1p1 */
        k_strncpy(part->name, dev->name, sizeof(part->name));
        size_t len = 0;
        while (part->name[len]) len++;
        if (dev->type == XSCORE_STORAGE_NVME) {
            if (len + 3 < sizeof(part->name)) {
                part->name[len++] = 'p';
                part->name[len++] = (char)('1' + dev->partition_count);
                part->name[len] = '\0';
            }
        } else {
            if (len + 2 < sizeof(part->name)) {
                part->name[len++] = (char)('1' + dev->partition_count);
                part->name[len] = '\0';
            }
        }

        /* Register partition device node in VFS /dev */
        (void)xscore_vfs_add_character_device(part->name, 0660);

        xscore_serial_write("[STORAGE] Found MBR Partition: ");
        xscore_serial_write(part->name);
        xscore_serial_write("\n");

        dev->partition_count++;
    }
    return dev->partition_count > 0;
}

int xscore_partition_parse_gpt(struct xscore_storage_device *dev, const uint8_t *sector1, const uint8_t *entries) {
    if (!dev || !sector1 || !entries) return 0;
    const struct gpt_header *hdr = (const struct gpt_header *)sector1;

    /* Verify "EFI PART" signature */
    static const char gpt_magic[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
    for (int i = 0; i < 8; ++i) {
        if (hdr->signature[i] != gpt_magic[i]) return 0;
    }

    uint32_t num_entries = hdr->num_partition_entries;
    if (num_entries > XSCORE_STORAGE_MAX_PARTITIONS) {
        num_entries = XSCORE_STORAGE_MAX_PARTITIONS;
    }
    uint32_t entry_size = hdr->sizeof_partition_entry;
    if (entry_size < sizeof(struct gpt_partition_entry)) {
        entry_size = sizeof(struct gpt_partition_entry);
    }

    dev->partition_count = 0;

    for (uint32_t i = 0; i < num_entries; ++i) {
        const struct gpt_partition_entry *entry =
            (const struct gpt_partition_entry *)(entries + i * entry_size);

        /* Check for empty GUID (all zeros) */
        int all_zero = 1;
        for (int b = 0; b < 16; ++b) {
            if (entry->type_guid[b] != 0) { all_zero = 0; break; }
        }
        if (all_zero || entry->ending_lba < entry->starting_lba) continue;

        struct xscore_partition *part = &dev->partitions[dev->partition_count];
        memset(part, 0, sizeof(*part));

        part->start_lba = entry->starting_lba;
        part->sector_count = entry->ending_lba - entry->starting_lba + 1;
        part->sector_size = dev->sector_size ? dev->sector_size : 512;
        part->active = 1;
        part->is_gpt = 1;
        memcpy(part->guid, entry->type_guid, 16);
        memcpy(part->unique_guid, entry->unique_partition_guid, 16);

        /* Generate partition name, e.g. /dev/sda1 or /dev/nvme0n1p1 */
        k_strncpy(part->name, dev->name, sizeof(part->name));
        size_t len = 0;
        while (part->name[len]) len++;
        if (dev->type == XSCORE_STORAGE_NVME) {
            if (len + 3 < sizeof(part->name)) {
                part->name[len++] = 'p';
                part->name[len++] = (char)('1' + dev->partition_count);
                part->name[len] = '\0';
            }
        } else {
            if (len + 2 < sizeof(part->name)) {
                part->name[len++] = (char)('1' + dev->partition_count);
                part->name[len] = '\0';
            }
        }

        (void)xscore_vfs_add_character_device(part->name, 0660);

        xscore_serial_write("[STORAGE] Found GPT Partition: ");
        xscore_serial_write(part->name);
        xscore_serial_write("\n");

        dev->partition_count++;
    }
    return dev->partition_count > 0;
}

int xscore_storage_register_device(struct xscore_storage_device *dev) {
    if (!dev || !dev->active) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&g_storage_lock);
    if (g_storage_device_count >= XSCORE_STORAGE_MAX_DEVICES) {
        xscore_spin_unlock_irqrestore(&g_storage_lock, flags);
        return 0;
    }

    struct xscore_storage_device *target = &g_storage_devices[g_storage_device_count++];
    memcpy(target, dev, sizeof(*target));

    /* Register canonical base block device node in /dev (e.g. /dev/sda, /dev/vda) */
    (void)xscore_vfs_add_character_device(target->name, 0660);

    xscore_serial_write("[STORAGE] Registered Block Device: ");
    xscore_serial_write(target->name);
    xscore_serial_write(" (Model: ");
    xscore_serial_write(target->model[0] ? target->model : "Generic Drive");
    xscore_serial_write(")\n");

    /* Probe for partitions by reading sector 0 */
    uint64_t sector_phys = xscore_pmm_allocate_page();
    if (sector_phys) {
        uint8_t *sector_buf = (uint8_t *)(uintptr_t)sector_phys;
        if (xscore_storage_read(target->name, 0, 1, sector_buf)) {
            /* Try MBR first */
            if (!xscore_partition_parse_mbr(target, sector_buf)) {
                /* If MBR had 0xEE protective or no MBR, try reading sector 1 for GPT */
                uint64_t gpt_phys = xscore_pmm_allocate_page();
                if (gpt_phys) {
                    uint8_t *gpt_buf = (uint8_t *)(uintptr_t)gpt_phys;
                    if (xscore_storage_read(target->name, 1, 1, gpt_buf)) {
                        /* Read GPT entries at LBA 2 */
                        uint8_t *entries_buf = (uint8_t *)(uintptr_t)sector_phys;
                        if (xscore_storage_read(target->name, 2, 4, entries_buf)) {
                            (void)xscore_partition_parse_gpt(target, gpt_buf, entries_buf);
                        }
                    }
                    (void)xscore_pmm_free_page(gpt_phys);
                }
            }
        }
        (void)xscore_pmm_free_page(sector_phys);
    }

    xscore_spin_unlock_irqrestore(&g_storage_lock, flags);
    return 1;
}

int xscore_storage_initialize(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&g_storage_lock);
    if (g_storage_initialized) {
        xscore_spin_unlock_irqrestore(&g_storage_lock, flags);
        return 1;
    }
    g_storage_device_count = 0;
    memset(g_storage_devices, 0, sizeof(g_storage_devices));
    g_storage_initialized = 1;
    xscore_spin_unlock_irqrestore(&g_storage_lock, flags);

    xscore_serial_write("[STORAGE] Initializing Universal Storage Subsystem (AHCI/NVMe/VirtIO/IDE)...\n");

    /* 1. Initialize AHCI / SATA */
    (void)xscore_ahci_initialize();

    /* 2. Initialize NVMe */
    (void)xscore_nvme_initialize();

    /* Register NVMe drive if present */
    if (xscore_nvme_present()) {
        const struct xscore_nvme_controller_info *ninfo = xscore_nvme_get_info();
        if (ninfo && ninfo->namespace_count > 0) {
            struct xscore_storage_device ndev;
            memset(&ndev, 0, sizeof(ndev));
            ndev.type = XSCORE_STORAGE_NVME;
            ndev.active = 1;
            k_strncpy(ndev.name, "/dev/nvme0n1", sizeof(ndev.name));
            ndev.sector_size = ninfo->namespaces[0].lba_size_bytes;
            ndev.total_sectors = ninfo->namespaces[0].capacity_lbas;
            k_strncpy(ndev.model, ninfo->model, sizeof(ndev.model));
            k_strncpy(ndev.serial, ninfo->serial, sizeof(ndev.serial));
            k_strncpy(ndev.firmware, ninfo->firmware, sizeof(ndev.firmware));
            (void)xscore_storage_register_device(&ndev);
        }
    }

    /* 3. Initialize VirtIO-Block */
    (void)xscore_virtio_blk_initialize();
    if (xscore_virtio_blk_present()) {
        struct xscore_storage_device vdev;
        memset(&vdev, 0, sizeof(vdev));
        vdev.type = XSCORE_STORAGE_VIRTIO;
        vdev.active = 1;
        k_strncpy(vdev.name, "/dev/vda", sizeof(vdev.name));
        vdev.sector_size = 512;
        vdev.total_sectors = xscore_virtio_blk_sector_count();
        k_strncpy(vdev.model, "QEMU VirtIO Block Device", sizeof(vdev.model));
        (void)xscore_storage_register_device(&vdev);
    }

    return g_storage_device_count > 0;
}

uint32_t xscore_storage_device_count(void) {
    return g_storage_device_count;
}

const struct xscore_storage_device *xscore_storage_get_device(uint32_t index) {
    if (index < g_storage_device_count) return &g_storage_devices[index];
    return 0;
}

const struct xscore_storage_device *xscore_storage_find_by_name(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < g_storage_device_count; ++i) {
        if (k_strcmp(g_storage_devices[i].name, name) == 0) {
            return &g_storage_devices[i];
        }
        /* Check partition names */
        for (uint32_t p = 0; p < g_storage_devices[i].partition_count; ++p) {
            if (k_strcmp(g_storage_devices[i].partitions[p].name, name) == 0) {
                return &g_storage_devices[i];
            }
        }
    }
    return 0;
}

int xscore_storage_read(const char *dev_name, uint64_t lba, uint32_t count, void *buffer) {
    if (!dev_name || !buffer || count == 0) return 0;
    const struct xscore_storage_device *dev = xscore_storage_find_by_name(dev_name);
    if (!dev || !dev->active) {
        /* Fallback direct to virtio or nvme */
        if (xscore_virtio_blk_present()) {
            for (uint32_t i = 0; i < count; ++i) {
                if (!xscore_virtio_blk_read_sector(lba + i, (uint8_t *)buffer + i * 512U)) return 0;
            }
            return 1;
        }
        return 0;
    }

    /* Check if target is a partition */
    uint64_t target_lba = lba;
    for (uint32_t p = 0; p < dev->partition_count; ++p) {
        if (k_strcmp(dev->partitions[p].name, dev_name) == 0) {
            target_lba = dev->partitions[p].start_lba + lba;
            break;
        }
    }

    switch (dev->type) {
        case XSCORE_STORAGE_AHCI:
            return xscore_ahci_read_sectors(dev->port_or_target, target_lba, count, buffer);
        case XSCORE_STORAGE_NVME:
            return xscore_nvme_read_blocks(1, target_lba, count, buffer);
        case XSCORE_STORAGE_VIRTIO:
            for (uint32_t i = 0; i < count; ++i) {
                if (!xscore_virtio_blk_read_sector(target_lba + i, (uint8_t *)buffer + i * 512U)) return 0;
            }
            return 1;
        default:
            return 0;
    }
}

int xscore_storage_write(const char *dev_name, uint64_t lba, uint32_t count, const void *buffer) {
    if (!dev_name || !buffer || count == 0) return 0;
    const struct xscore_storage_device *dev = xscore_storage_find_by_name(dev_name);
    if (!dev || !dev->active) {
        if (xscore_virtio_blk_present()) {
            for (uint32_t i = 0; i < count; ++i) {
                if (!xscore_virtio_blk_write_sector(lba + i, (const uint8_t *)buffer + i * 512U)) return 0;
            }
            return 1;
        }
        return 0;
    }

    uint64_t target_lba = lba;
    for (uint32_t p = 0; p < dev->partition_count; ++p) {
        if (k_strcmp(dev->partitions[p].name, dev_name) == 0) {
            target_lba = dev->partitions[p].start_lba + lba;
            break;
        }
    }

    switch (dev->type) {
        case XSCORE_STORAGE_AHCI:
            return xscore_ahci_write_sectors(dev->port_or_target, target_lba, count, buffer);
        case XSCORE_STORAGE_NVME:
            return xscore_nvme_write_blocks(1, target_lba, count, buffer);
        case XSCORE_STORAGE_VIRTIO:
            for (uint32_t i = 0; i < count; ++i) {
                if (!xscore_virtio_blk_write_sector(target_lba + i, (const uint8_t *)buffer + i * 512U)) return 0;
            }
            return 1;
        default:
            return 0;
    }
}

int xscore_storage_flush(const char *dev_name) {
    if (!dev_name) return 0;
    const struct xscore_storage_device *dev = xscore_storage_find_by_name(dev_name);
    if (!dev || !dev->active) return 1;

    switch (dev->type) {
        case XSCORE_STORAGE_AHCI:
            return xscore_ahci_flush(dev->port_or_target);
        case XSCORE_STORAGE_NVME:
            return xscore_nvme_flush(1);
        case XSCORE_STORAGE_VIRTIO:
            return 1;
        default:
            return 1;
    }
}


int xscore_storage_eject(const char *dev_name) {
    const struct xscore_storage_device *dev = xscore_storage_find_by_name(dev_name);
    if (!dev) return -1;
    // Logic for ejecting goes here.
    // For now, it's just a placeholder as we don't have driver-specific eject.
    return 0;
}
