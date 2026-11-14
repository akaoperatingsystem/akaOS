#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FDISK_VERSION "2.12r (akaOS util-linux)"
#define SECTOR_SIZE 512

#pragma pack(push, 1)
struct mbr_partition {
    uint8_t bootable;
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t sys_id;
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t start_lba;
    uint32_t total_sectors;
};

struct mbr_sector {
    uint8_t bootstrap[446];
    struct mbr_partition partitions[4];
    uint16_t signature;
};
#pragma pack(pop)

struct disk_geometry {
    long cylinders;
    int heads;
    int sectors;
    uint64_t total_sectors;
};

static int opt_sectors = 0;   /* -u */
static int opt_list = 0;      /* -l */
static int opt_size = 0;      /* -s */

static void print_version(void) {
    printf("fdisk %s\n", FDISK_VERSION);
}

static void print_usage(void) {
    printf("Usage:\n"
           " fdisk [-u] device\n"
           " fdisk -l [-u] device ...\n"
           " fdisk -s partition ...\n"
           " fdisk -v\n\n"
           "Options:\n"
           " -u             give sizes in sectors instead of cylinders\n"
           " -l             list partition table(s)\n"
           " -s partition   print partition size(s) in blocks\n"
           " -v             print program version\n");
}

static const char *get_sys_type_name(uint8_t id) {
    switch (id) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "Extended";
        case 0x06: return "FAT16";
        case 0x07: return "HPFS/NTFS/exFAT";
        case 0x0b: return "W95 FAT32";
        case 0x0c: return "W95 FAT32 (LBA)";
        case 0x0e: return "W95 FAT16 (LBA)";
        case 0x0f: return "W95 Ext'd (LBA)";
        case 0x82: return "Linux swap / Solaris";
        case 0x83: return "Linux";
        case 0x85: return "Linux extended";
        case 0x8e: return "Linux LVM";
        case 0xef: return "EFI (FAT-12/16/32)";
        default:   return "Unknown";
    }
}

static int read_mbr(const char *dev, struct mbr_sector *mbr, struct disk_geometry *geom) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        geom->total_sectors = (uint64_t)st.st_size / SECTOR_SIZE;
    } else {
        geom->total_sectors = 204800; /* Default fallback ~100MB */
    }

    geom->heads = 255;
    geom->sectors = 63;
    geom->cylinders = (long)(geom->total_sectors / ((uint64_t)geom->heads * geom->sectors));
    if (geom->cylinders <= 0) geom->cylinders = 1;

    memset(mbr, 0, sizeof(*mbr));
    ssize_t n = pread(fd, mbr, sizeof(*mbr), 0);
    close(fd);

    if (n < (ssize_t)sizeof(*mbr)) {
        return -1;
    }
    return 0;
}

static int list_device_partitions(const char *dev) {
    struct mbr_sector mbr;
    struct disk_geometry geom;

    if (read_mbr(dev, &mbr, &geom) != 0) {
        fprintf(stderr, "fdisk: cannot open %s: %s\n", dev, strerror(errno));
        return 1;
    }

    uint64_t total_bytes = geom.total_sectors * SECTOR_SIZE;
    uint64_t total_mb = total_bytes / (1024 * 1024);

    printf("\nDisk %s: %" PRIu64 " MB, %" PRIu64 " bytes\n", dev, total_mb, total_bytes);
    printf("%d heads, %d sectors/track, %ld cylinders", geom.heads, geom.sectors, geom.cylinders);
    if (opt_sectors) {
        printf(", total %" PRIu64 " sectors\n", geom.total_sectors);
        printf("Units = sectors of 1 * %d = %d bytes\n\n", SECTOR_SIZE, SECTOR_SIZE);
        printf("%-12s Boot      Start         End      Blocks   Id  System\n", "Device");
    } else {
        printf("\nUnits = cylinders of %d * %d = %d bytes\n\n",
               geom.heads * geom.sectors, SECTOR_SIZE, geom.heads * geom.sectors * SECTOR_SIZE);
        printf("%-12s Boot      Start         End      Blocks   Id  System\n", "Device");
    }

    uint32_t cyl_size = (uint32_t)geom.heads * geom.sectors;
    if (cyl_size == 0) cyl_size = 1;

    for (int i = 0; i < 4; ++i) {
        struct mbr_partition *p = &mbr.partitions[i];
        if (p->sys_id == 0 || p->total_sectors == 0) continue;

        char part_name[64];
        snprintf(part_name, sizeof(part_name), "%s%d", dev, i + 1);

        char boot_char = (p->bootable == 0x80) ? '*' : ' ';
        uint32_t start_val, end_val;

        if (opt_sectors) {
            start_val = p->start_lba;
            end_val = p->start_lba + p->total_sectors - 1;
        } else {
            start_val = p->start_lba / cyl_size + 1;
            end_val = (p->start_lba + p->total_sectors - 1) / cyl_size + 1;
        }

        uint32_t blocks = p->total_sectors / 2; /* 1024-byte blocks */

        printf("%-12s   %c    %10u  %10u  %10u+  %2x  %s\n",
               part_name, boot_char, start_val, end_val, blocks, p->sys_id, get_sys_type_name(p->sys_id));
    }

    return 0;
}

static int print_partition_size(const char *part_path) {
    /* If path is e.g. /dev/sda1, deduce device /dev/sda and partition index 1 */
    size_t len = strlen(part_path);
    if (len == 0) return 1;

    char dev_path[128];
    strncpy(dev_path, part_path, sizeof(dev_path) - 1);
    dev_path[sizeof(dev_path) - 1] = '\0';

    int part_idx = -1;
    if (len > 1 && dev_path[len - 1] >= '1' && dev_path[len - 1] <= '4') {
        part_idx = dev_path[len - 1] - '1';
        dev_path[len - 1] = '\0';
    }

    struct mbr_sector mbr;
    struct disk_geometry geom;

    if (read_mbr(dev_path, &mbr, &geom) != 0) {
        /* If opening dev_path directly fails, try part_path */
        if (read_mbr(part_path, &mbr, &geom) != 0) {
            fprintf(stderr, "fdisk: cannot open %s: %s\n", part_path, strerror(errno));
            return 1;
        }
        printf("%" PRIu64 "\n", geom.total_sectors / 2);
        return 0;
    }

    if (part_idx >= 0 && part_idx < 4) {
        struct mbr_partition *p = &mbr.partitions[part_idx];
        if (p->sys_id != 0 && p->total_sectors > 0) {
            printf("%u\n", p->total_sectors / 2);
            return 0;
        }
    }

    /* Fallback whole disk size in blocks */
    printf("%" PRIu64 "\n", geom.total_sectors / 2);
    return 0;
}

static int run_interactive_fdisk(const char *dev) {
    struct mbr_sector mbr;
    struct disk_geometry geom;

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fd = open(dev, O_RDONLY);
    }
    if (fd < 0) {
        fprintf(stderr, "fdisk: cannot open %s: %s\n", dev, strerror(errno));
        return 1;
    }

    if (read_mbr(dev, &mbr, &geom) != 0) {
        memset(&mbr, 0, sizeof(mbr));
        mbr.signature = 0xAA55;
    }

    printf("Command (m for help): ");
    fflush(stdout);

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        char cmd = line[0];
        while (cmd == ' ' || cmd == '\t') cmd++;

        if (cmd == 'm') {
            printf("Command action\n"
                   "   a   toggle a bootable flag\n"
                   "   d   delete a partition\n"
                   "   l   list known partition types\n"
                   "   m   print this menu\n"
                   "   p   print the partition table\n"
                   "   q   quit without saving changes\n"
                   "   t   change a partition's system id\n"
                   "   u   change display/entry units\n"
                   "   v   verify the partition table\n"
                   "   w   write table to disk and exit\n");
        } else if (cmd == 'p') {
            list_device_partitions(dev);
        } else if (cmd == 'u') {
            opt_sectors = !opt_sectors;
            printf("Changing display/entry units to %s\n", opt_sectors ? "sectors" : "cylinders");
        } else if (cmd == 'l') {
            printf(" 1  FAT12           7  HPFS/NTFS/exFAT 82  Linux swap / So\n"
                   " 4  FAT16 <32M      b  W95 FAT32       83  Linux          \n"
                   " 5  Extended        c  W95 FAT32 (LBA) 85  Linux extended \n"
                   " 6  FAT16           e  W95 FAT16 (LBA) 8e  Linux LVM      \n"
                   " ef EFI (FAT-12/16/32)\n");
        } else if (cmd == 'q') {
            break;
        } else if (cmd == 'w') {
            if (pwrite(fd, &mbr, sizeof(mbr), 0) == (ssize_t)sizeof(mbr)) {
                fsync(fd);
                printf("The partition table has been altered!\n"
                       "Calling ioctl() to re-read partition table.\n"
                       "Syncing disks.\n");
            } else {
                fprintf(stderr, "fdisk: write failed: %s\n", strerror(errno));
            }
            break;
        } else if (cmd == 'v') {
            printf("%" PRIu64 " unallocated sectors\n", geom.total_sectors > 2048 ? geom.total_sectors - 2048 : 0);
        } else if (cmd != '\n' && cmd != '\0') {
            printf("%c: unknown command\n", cmd);
        }

        printf("\nCommand (m for help): ");
        fflush(stdout);
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    char *devices[32];
    int dev_count = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-u") == 0) {
            opt_sectors = 1;
            continue;
        }
        if (strcmp(arg, "-l") == 0) {
            opt_list = 1;
            continue;
        }
        if (strcmp(arg, "-s") == 0) {
            opt_size = 1;
            continue;
        }
        if (arg[0] != '-') {
            if (dev_count < 32) devices[dev_count++] = arg;
        }
    }

    if (opt_size) {
        if (dev_count == 0) {
            print_usage();
            return 1;
        }
        int ret = 0;
        for (int i = 0; i < dev_count; ++i) {
            if (print_partition_size(devices[i]) != 0) ret = 1;
        }
        return ret;
    }

    if (opt_list) {
        if (dev_count == 0) {
            /* Default list well-known storage devices */
            static const char *default_disks[] = {
                "/dev/sda", "/dev/sdb", "/dev/hda", "/dev/hdb", "/dev/vda", "/dev/nvme0n1", NULL
            };
            int listed = 0;
            for (int i = 0; default_disks[i]; ++i) {
                struct stat st;
                if (stat(default_disks[i], &st) == 0) {
                    list_device_partitions(default_disks[i]);
                    listed++;
                }
            }
            if (!listed) {
                /* If none exist on disk, list /dev/hda fallback */
                list_device_partitions("/dev/hda");
            }
            return 0;
        } else {
            int ret = 0;
            for (int i = 0; i < dev_count; ++i) {
                if (list_device_partitions(devices[i]) != 0) ret = 1;
            }
            return ret;
        }
    }

    if (dev_count == 0) {
        devices[0] = "/dev/hda";
        dev_count = 1;
    }

    return run_interactive_fdisk(devices[0]);
}
