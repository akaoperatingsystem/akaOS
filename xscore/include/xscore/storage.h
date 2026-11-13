#ifndef XSCORE_STORAGE_H
#define XSCORE_STORAGE_H

#include <stdint.h>
#include <stddef.h>

#define XSCORE_STORAGE_MAX_DEVICES    16U
#define XSCORE_STORAGE_MAX_PARTITIONS 16U

/* Storage Controller / Bus Interface Kinds */
enum xscore_storage_type {
    XSCORE_STORAGE_NONE = 0,
    XSCORE_STORAGE_AHCI,       /* SATA SSD / Mechanical HDD -> /dev/sda, /dev/sdb */
    XSCORE_STORAGE_NVME,       /* PCIe M.2 / U.2 NVMe SSD   -> /dev/nvme0n1... */
    XSCORE_STORAGE_VIRTIO,     /* Cloud / Hypervisor VirtIO -> /dev/vda, /dev/vdb */
    XSCORE_STORAGE_IDE,        /* Legacy Parallel ATA / IDE -> /dev/hda, /dev/hdb */
    XSCORE_STORAGE_USB,        /* USB Mass Storage (BOT/UAS)-> /dev/sd* */
};

/* Partition Entry (MBR or GPT) */
struct xscore_partition {
    char     name[32];         /* Canonical node name, e.g. "/dev/sda1", "/dev/nvme0n1p1" */
    uint64_t start_lba;        /* Starting LBA on physical device */
    uint64_t sector_count;     /* Size in sectors */
    uint32_t sector_size;      /* Sector size in bytes (512 or 4096) */
    uint8_t  type_mbr;         /* MBR partition type byte (e.g. 0x83 Linux, 0x0C FAT32) */
    uint8_t  active;           /* 1 if partition is valid / present */
    uint8_t  bootable;         /* Boot flag (0x80 for active MBR boot partition) */
    uint8_t  is_gpt;           /* 1 if parsed from GPT, 0 if MBR */
    uint8_t  guid[16];         /* GPT Partition Type GUID (if is_gpt) */
    uint8_t  unique_guid[16];  /* GPT Unique Partition GUID (if is_gpt) */
};

/* Physical or Virtual Storage Drive */
struct xscore_storage_device {
    char     name[32];         /* Base drive name, e.g. "/dev/sda", "/dev/nvme0n1", "/dev/vda" */
    enum xscore_storage_type type;
    uint8_t  active;
    uint8_t  controller_index;
    uint8_t  port_or_target;
    uint32_t sector_size;      /* 512 or 4096 */
    uint64_t total_sectors;    /* Total addressable LBA sectors */
    char     model[41];
    char     serial[21];
    char     firmware[9];
    uint32_t partition_count;
    struct xscore_partition partitions[XSCORE_STORAGE_MAX_PARTITIONS];
};

/* MBR Partition Table Entry (on-disk format at 0x1BE + 16*i) */
#pragma pack(push, 1)
struct mbr_partition_record {
    uint8_t  boot_indicator;   /* 0x80 = bootable, 0x00 = inactive */
    uint8_t  start_head;
    uint8_t  start_sector:6;
    uint8_t  start_cylinder_hi:2;
    uint8_t  start_cylinder_lo;
    uint8_t  os_type;          /* Partition type */
    uint8_t  end_head;
    uint8_t  end_sector:6;
    uint8_t  end_cylinder_hi:2;
    uint8_t  end_cylinder_lo;
    uint32_t starting_lba;
    uint32_t size_in_lba;
};

/* GPT Header Format (LBA 1) */
struct gpt_header {
    char     signature[8];     /* "EFI PART" */
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;      /* LBA 1 */
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba; /* LBA 2 */
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry; /* Typically 128 */
    uint32_t partition_entry_array_crc32;
};

/* GPT Partition Entry Format (typically 128 bytes) */
struct gpt_partition_entry {
    uint8_t  type_guid[16];
    uint8_t  unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36]; /* UTF-16LE name */
};
#pragma pack(pop)

/* Core Unified Storage Subsystem APIs */
int xscore_storage_initialize(void);
uint32_t xscore_storage_device_count(void);
const struct xscore_storage_device *xscore_storage_get_device(uint32_t index);
const struct xscore_storage_device *xscore_storage_find_by_name(const char *name);

/* Block I/O */
int xscore_storage_read(const char *dev_name, uint64_t lba, uint32_t count, void *buffer);
int xscore_storage_write(const char *dev_name, uint64_t lba, uint32_t count, const void *buffer);
int xscore_storage_flush(const char *dev_name);
int xscore_storage_eject(const char *dev_name);

/* Partition Parsers */
int xscore_partition_parse_mbr(struct xscore_storage_device *dev, const uint8_t *sector0);
int xscore_partition_parse_gpt(struct xscore_storage_device *dev, const uint8_t *sector1, const uint8_t *entries);

/* Device Driver Registration Helper */
int xscore_storage_register_device(struct xscore_storage_device *dev);

#endif /* XSCORE_STORAGE_H */
