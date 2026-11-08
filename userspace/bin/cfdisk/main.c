#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_DEVICE "/dev/hda"
#define SECTOR_SIZE 512

/* Exit status codes per man page */
#define EXIT_OK 0
#define EXIT_INVOCATION_ERROR 1
#define EXIT_IO_ERROR 2
#define EXIT_NO_GEOMETRY 3
#define EXIT_BAD_PARTITION_TABLE 4

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

struct partition_entry {
    int num;
    int is_logical;
    int is_free;
    int is_unusable;
    int bootable;
    int not_compatible;
    uint8_t sys_id;
    char fs_name[32];
    uint32_t start_sector;
    uint32_t end_sector;
    uint32_t sector_count;
};

#define MAX_PARTS 64
static struct partition_entry part_table[MAX_PARTS];
static int part_count = 0;
static struct disk_geometry geom;
static char device_name[128] = DEFAULT_DEVICE;

static int opt_arrow = 0;
static int opt_guess = 0;
static int opt_zero = 0;
static char opt_print[32] = "";

static long user_cylinders = 0;
static int user_heads = 0;
static int user_sectors = 0;

static struct mbr_sector disk_mbr;
static int disk_fd = -1;

static void print_version(void) {
    printf("cfdisk (akaOS util-linux) 2.12r\n"
           "Copyright (C) 1994-2002 Kevin E. Martin & util-linux team\n");
}

static const char *get_fs_name(uint8_t id) {
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

static int open_disk_device(const char *dev, int zero_mode) {
    disk_fd = open(dev, O_RDWR);
    if (disk_fd < 0) {
        disk_fd = open(dev, O_RDONLY);
    }
    if (disk_fd < 0) {
        /* If device cannot be opened and is default /dev/hda, fallback to virtual disk if available */
        return -1;
    }

    struct stat st;
    if (fstat(disk_fd, &st) == 0 && st.st_size > 0) {
        geom.total_sectors = (uint64_t)st.st_size / SECTOR_SIZE;
    } else {
        /* Default assume 100MB or geometry-based */
        geom.total_sectors = 204800;
    }

    geom.heads = user_heads > 0 ? user_heads : 255;
    geom.sectors = user_sectors > 0 ? user_sectors : 63;
    if (user_cylinders > 0) {
        geom.cylinders = user_cylinders;
    } else {
        geom.cylinders = (long)(geom.total_sectors / ((uint64_t)geom.heads * geom.sectors));
        if (geom.cylinders <= 0) geom.cylinders = 1;
    }

    if (zero_mode) {
        memset(&disk_mbr, 0, sizeof(disk_mbr));
        disk_mbr.signature = 0xAA55;
    } else {
        ssize_t n = pread(disk_fd, &disk_mbr, sizeof(disk_mbr), 0);
        if (n < (ssize_t)sizeof(disk_mbr)) {
            memset(&disk_mbr, 0, sizeof(disk_mbr));
            disk_mbr.signature = 0xAA55;
        }
    }

    return 0;
}

static void build_partition_table(void) {
    part_count = 0;
    uint32_t cur_sector = 1;

    for (int i = 0; i < 4; ++i) {
        struct mbr_partition *p = &disk_mbr.partitions[i];
        if (p->sys_id != 0 && p->total_sectors > 0) {
            if (p->start_lba > cur_sector) {
                /* Free space gap */
                struct partition_entry *free_sp = &part_table[part_count++];
                free_sp->num = -1;
                free_sp->is_free = 1;
                free_sp->is_unusable = 0;
                free_sp->start_sector = cur_sector;
                free_sp->end_sector = p->start_lba - 1;
                free_sp->sector_count = free_sp->end_sector - free_sp->start_sector + 1;
                strcpy(free_sp->fs_name, "Free Space");
            }
            struct partition_entry *ent = &part_table[part_count++];
            ent->num = i + 1;
            ent->is_logical = 0;
            ent->is_free = 0;
            ent->is_unusable = 0;
            ent->bootable = (p->bootable == 0x80);
            ent->sys_id = p->sys_id;
            ent->start_sector = p->start_lba;
            ent->end_sector = p->start_lba + p->total_sectors - 1;
            ent->sector_count = p->total_sectors;
            strncpy(ent->fs_name, get_fs_name(p->sys_id), sizeof(ent->fs_name) - 1);
            cur_sector = ent->end_sector + 1;
        }
    }

    if (cur_sector < geom.total_sectors) {
        struct partition_entry *free_sp = &part_table[part_count++];
        free_sp->num = -1;
        free_sp->is_free = 1;
        free_sp->is_unusable = 0;
        free_sp->start_sector = cur_sector;
        free_sp->end_sector = (uint32_t)(geom.total_sectors - 1);
        free_sp->sector_count = free_sp->end_sector - free_sp->start_sector + 1;
        strcpy(free_sp->fs_name, "Free Space");
    }

    if (part_count == 0) {
        struct partition_entry *free_sp = &part_table[part_count++];
        free_sp->num = -1;
        free_sp->is_free = 1;
        free_sp->is_unusable = 0;
        free_sp->start_sector = 1;
        free_sp->end_sector = (uint32_t)(geom.total_sectors - 1);
        free_sp->sector_count = free_sp->end_sector - free_sp->start_sector + 1;
        strcpy(free_sp->fs_name, "Free Space");
    }
}

static void print_format_raw_data(void) {
    uint8_t *raw = (uint8_t *)&disk_mbr;
    for (int i = 0; i < 512; i += 16) {
        printf("%04X: ", i);
        for (int j = 0; j < 16; ++j) {
            printf("%02X ", raw[i + j]);
        }
        printf(" |");
        for (int j = 0; j < 16; ++j) {
            uint8_t c = raw[i + j];
            putchar((c >= 32 && c <= 126) ? (char)c : '.');
        }
        printf("|\n");
    }
}

static void print_format_sector_order(void) {
    printf("Partition Table in Sector Order\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("No  Type     Start      End        Offset   Length     Filesystem (ID)     Flags\n");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < part_count; ++i) {
        struct partition_entry *p = &part_table[i];
        if (p->is_free) {
            printf("--  Free     %-10u %-10u %-8u %-10u Free Space (00)     (00)\n",
                   p->start_sector, p->end_sector, 0, p->sector_count);
        } else {
            printf("%-2d  Primary  %-10u %-10u %-8u %-10u %-19s (%02X)\n",
                   p->num, p->start_sector, p->end_sector, p->start_sector,
                   p->sector_count, p->fs_name, p->bootable ? 0x80 : 0x00);
        }
    }
}

static void print_format_raw(void) {
    printf("Partition Table in Raw Format\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("No  Flags  Start (C,H,S)      End (C,H,S)        StartSect  TotalSects  ID\n");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < 4; ++i) {
        struct mbr_partition *p = &disk_mbr.partitions[i];
        if (p->sys_id != 0) {
            int sc = p->start_cylinder | ((p->start_sector & 0xC0) << 2);
            int ss = p->start_sector & 0x3F;
            int ec = p->end_cylinder | ((p->end_sector & 0xC0) << 2);
            int es = p->end_sector & 0x3F;
            printf("%-2d  0x%02X   (%4d,%3d,%2d)    (%4d,%3d,%2d)    %-10u %-10u  0x%02X\n",
                   i + 1, p->bootable, sc, p->start_head, ss, ec, p->end_head, es,
                   p->start_lba, p->total_sectors, p->sys_id);
        }
    }
}

static void run_tui(void) {
    int cur_idx = 0;
    int units_mode = 0; /* 0: MB, 1: Sectors, 2: Cylinders */

    /* Enter raw / non-canonical mode if interactive */
    struct termios orig_term, raw_term;
    int is_tty = isatty(STDIN_FILENO);
    if (is_tty) {
        tcgetattr(STDIN_FILENO, &orig_term);
        raw_term = orig_term;
        raw_term.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);
    }

    int running = 1;
    while (running) {
        /* Clear screen and redraw header */
        printf("\033[2J\033[H");
        printf("                                 cfdisk 2.12r\n\n");
        printf("                              Disk Drive: %s\n", device_name);
        printf("                       Size: %" PRIu64 " bytes, %" PRIu64 " MB\n",
               geom.total_sectors * SECTOR_SIZE,
               (geom.total_sectors * SECTOR_SIZE) / (1024 * 1024));
        printf("             Heads: %d   Sectors per Track: %d   Cylinders: %ld\n\n",
               geom.heads, geom.sectors, geom.cylinders);

        printf("    Name        Flags      Part Type  FS Type          [Label]        Size (MB)\n");
        printf(" -------------------------------------------------------------------------------\n");

        for (int i = 0; i < part_count; ++i) {
            struct partition_entry *p = &part_table[i];
            char dev_entry[32];
            if (p->num > 0) {
                snprintf(dev_entry, sizeof(dev_entry), "%s%d", device_name, p->num);
            } else {
                snprintf(dev_entry, sizeof(dev_entry), "   Pri/Log");
            }

            char flag_str[16] = "";
            if (p->bootable) strcpy(flag_str, "Boot");
            if (p->not_compatible) {
                if (flag_str[0]) strcat(flag_str, ", NC");
                else strcpy(flag_str, "NC");
            }

            char type_str[16] = "Primary";
            if (p->is_free) strcpy(type_str, "Pri/Log");
            else if (p->is_logical) strcpy(type_str, "Logical");

            uint64_t bytes = (uint64_t)p->sector_count * SECTOR_SIZE;
            uint32_t mb_whole = (uint32_t)(bytes / (1024 * 1024));
            uint32_t mb_frac = (uint32_t)(((bytes % (1024 * 1024)) * 100) / (1024 * 1024));

            if (i == cur_idx) {
                if (opt_arrow) {
                    printf(" -> ");
                } else {
                    printf("\033[7m"); /* reverse video */
                }
            } else {
                printf("    ");
            }

            printf("%-11s %-10s %-10s %-16s                %5u.%02u",
                   dev_entry, flag_str, type_str, p->fs_name, mb_whole, mb_frac);

            if (i == cur_idx && !opt_arrow) {
                printf("\033[0m");
            }
            printf("\n");
        }

        printf(" -------------------------------------------------------------------------------\n");
        printf("     [Bootable]  [ Delete ]  [  Help  ]  [Maximize]  [ Print  ]\n");
        printf("     [  Quit  ]  [  Type  ]  [ Units  ]  [ Write  ]\n\n");
        printf("                     Toggle bootable flag of current partition\n");
        fflush(stdout);

        if (!is_tty) {
            break;
        }

        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) <= 0) break;

        if (ch == '\033') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'A') { /* Up Arrow */
                        if (cur_idx > 0) cur_idx--;
                    } else if (seq[1] == 'B') { /* Down Arrow */
                        if (cur_idx < part_count - 1) cur_idx++;
                    }
                }
            }
            continue;
        }

        switch (ch) {
            case 'q':
            case 'Q':
                running = 0;
                break;
            case 'b':
            case 'B':
                if (cur_idx < part_count && !part_table[cur_idx].is_free) {
                    part_table[cur_idx].bootable = !part_table[cur_idx].bootable;
                    if (part_table[cur_idx].num > 0 && part_table[cur_idx].num <= 4) {
                        disk_mbr.partitions[part_table[cur_idx].num - 1].bootable =
                            part_table[cur_idx].bootable ? 0x80 : 0x00;
                    }
                }
                break;
            case 'd':
            case 'D':
                if (cur_idx < part_count && !part_table[cur_idx].is_free) {
                    int pnum = part_table[cur_idx].num;
                    if (pnum > 0 && pnum <= 4) {
                        memset(&disk_mbr.partitions[pnum - 1], 0, sizeof(struct mbr_partition));
                        build_partition_table();
                        if (cur_idx >= part_count) cur_idx = part_count - 1;
                    }
                }
                break;
            case 'h':
            case 'H':
            case '?':
                printf("\033[2J\033[HHelp Screen\n\n"
                       "b: Toggle bootable flag\n"
                       "d: Delete current partition\n"
                       "g: Change disk geometry\n"
                       "m: Maximize disk usage\n"
                       "n: Create new partition\n"
                       "p: Print partition table\n"
                       "q: Quit without saving\n"
                       "t: Change filesystem type\n"
                       "u: Change display units\n"
                       "W: Write partition table to disk\n\n"
                       "Press any key to return...");
                fflush(stdout);
                char dummy;
                (void)read(STDIN_FILENO, &dummy, 1);
                break;
            case 'p':
            case 'P':
                printf("\033[2J\033[H");
                print_format_sector_order();
                printf("\nPress any key to return...");
                fflush(stdout);
                (void)read(STDIN_FILENO, &dummy, 1);
                break;
            case 'u':
            case 'U':
                units_mode = (units_mode + 1) % 3;
                break;
            case 'w':
            case 'W':
                if (disk_fd >= 0) {
                    pwrite(disk_fd, &disk_mbr, sizeof(disk_mbr), 0);
                    fsync(disk_fd);
                }
                running = 0;
                break;
            case 12: /* CTRL-L */
                break;
            default:
                break;
        }
    }

    if (is_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return EXIT_OK;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: cfdisk [options] [device]\n\n"
                   "Options:\n"
                   "  -a               Use arrow cursor instead of reverse video\n"
                   "  -g               Guess geometry from partition table\n"
                   "  -v               Print version number and copyright\n"
                   "  -z               Start with zeroed partition table\n"
                   "  -c cylinders     Override cylinders\n"
                   "  -h heads         Override heads\n"
                   "  -s sectors       Override sectors per track\n"
                   "  -P opt           Print partition table in format (r, s, t)\n");
            return EXIT_OK;
        }
        if (strcmp(arg, "-a") == 0) {
            opt_arrow = 1;
            continue;
        }
        if (strcmp(arg, "-g") == 0) {
            opt_guess = 1;
            continue;
        }
        if (strcmp(arg, "-z") == 0) {
            opt_zero = 1;
            continue;
        }
        if (strcmp(arg, "-c") == 0 && i + 1 < argc) {
            user_cylinders = atol(argv[++i]);
            continue;
        }
        if (strcmp(arg, "-h") == 0 && i + 1 < argc) {
            user_heads = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "-s") == 0 && i + 1 < argc) {
            user_sectors = atoi(argv[++i]);
            continue;
        }
        if (strcmp(arg, "-P") == 0 && i + 1 < argc) {
            strncpy(opt_print, argv[++i], sizeof(opt_print) - 1);
            continue;
        }
        if (strncmp(arg, "-P", 2) == 0 && strlen(arg) > 2) {
            strncpy(opt_print, arg + 2, sizeof(opt_print) - 1);
            continue;
        }
        if (arg[0] != '-') {
            strncpy(device_name, arg, sizeof(device_name) - 1);
            continue;
        }
    }

    if (open_disk_device(device_name, opt_zero) != 0) {
        /* If opening device fails */
        fprintf(stderr, "cfdisk: cannot open %s: %s\n", device_name, strerror(errno));
        return EXIT_IO_ERROR;
    }

    build_partition_table();

    if (opt_print[0] != '\0') {
        for (size_t k = 0; opt_print[k]; ++k) {
            switch (opt_print[k]) {
                case 'r':
                    print_format_raw_data();
                    break;
                case 's':
                    print_format_sector_order();
                    break;
                case 't':
                    print_format_raw();
                    break;
                default:
                    fprintf(stderr, "cfdisk: unknown print format '%c'\n", opt_print[k]);
                    return EXIT_INVOCATION_ERROR;
            }
        }
        if (disk_fd >= 0) close(disk_fd);
        return EXIT_OK;
    }

    run_tui();

    if (disk_fd >= 0) close(disk_fd);
    return EXIT_OK;
}
