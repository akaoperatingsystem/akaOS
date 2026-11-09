#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define DF_VERSION "8.32 (akaOS coreutils)"

/* Options */
static int opt_all = 0;              /* -a, --all */
static int opt_human = 0;            /* -h, --human-readable (1024) */
static int opt_si = 0;               /* -H, --si (1000) */
static int opt_inodes = 0;           /* -i, --inodes */
static int opt_kilo = 0;             /* -k, --kilobytes */
static int opt_local = 0;            /* -l, --local */
static int opt_mega = 0;             /* -m, --megabytes */
static int opt_posix = 0;            /* -P, --portability */
static int opt_print_type = 0;       /* -T, --print-type */
static const char *opt_type = NULL;  /* -t, --type=TYPE */
static const char *opt_exclude = NULL; /* -x, --exclude-type=TYPE */

static void print_version(void) {
    printf("df %s\n", DF_VERSION);
}

static void print_usage(void) {
    printf("Usage: df [OPTION]... [FILE]...\n"
           "Show information about the file system on which each FILE resides,\n"
           "or all file systems by default.\n\n"
           "  -a, --all             include pseudo, duplicate, inaccessible file systems\n"
           "  -h, --human-readable  print sizes in powers of 1024 (e.g., 1023M)\n"
           "  -H, --si              print sizes in powers of 1000 (e.g., 1.1G)\n"
           "  -i, --inodes          list inode information instead of block usage\n"
           "  -k                    like --block-size=1K\n"
           "  -l, --local           limit listing to local file systems\n"
           "  -m                    like --block-size=1M\n"
           "      --no-sync         do not invoke sync before getting usage info (default)\n"
           "  -P, --portability     use the POSIX output format\n"
           "      --sync            invoke sync before getting usage info\n"
           "  -t, --type=TYPE       limit listing to file systems of type TYPE\n"
           "  -T, --print-type      print file system type\n"
           "  -x, --exclude-type=TYPE limit listing to file systems not of type TYPE\n"
           "  -v                    (ignored)\n"
           "      --help            display this help and exit\n"
           "      --version         output version information and exit\n");
}

static void format_human_size(uint64_t bytes, char *buf, size_t sz, uint64_t base) {
    if (bytes >= base * base * base) {
        uint64_t gb = bytes / (base * base * base);
        uint64_t frac = (bytes % (base * base * base)) / (base * base * (base / 10));
        if (gb < 10) snprintf(buf, sz, "%llu.%lluG", (unsigned long long)gb, (unsigned long long)frac);
        else snprintf(buf, sz, "%lluG", (unsigned long long)gb);
    } else if (bytes >= base * base) {
        uint64_t mb = bytes / (base * base);
        uint64_t frac = (bytes % (base * base)) / (base * (base / 10));
        if (mb < 10) snprintf(buf, sz, "%llu.%lluM", (unsigned long long)mb, (unsigned long long)frac);
        else snprintf(buf, sz, "%lluM", (unsigned long long)mb);
    } else if (bytes >= base) {
        uint64_t kb = bytes / base;
        snprintf(buf, sz, "%lluK", (unsigned long long)kb);
    } else {
        snprintf(buf, sz, "%lluB", (unsigned long long)bytes);
    }
}

static void format_value(uint64_t val, int is_bytes, char *buf, size_t sz) {
    (void)is_bytes;
    if (opt_inodes) {
        snprintf(buf, sz, "%llu", (unsigned long long)val);
        return;
    }

    if (opt_human) {
        format_human_size(val, buf, sz, 1024ULL);
    } else if (opt_si) {
        format_human_size(val, buf, sz, 1000ULL);
    } else if (opt_mega) {
        uint64_t mb = (val + 1024ULL * 1024ULL - 1) / (1024ULL * 1024ULL);
        snprintf(buf, sz, "%llu", (unsigned long long)mb);
    } else {
        /* Default 1K blocks */
        uint64_t kb = (val + 1023ULL) / 1024ULL;
        snprintf(buf, sz, "%llu", (unsigned long long)kb);
    }
}

static int parse_mount_line(char *line, char *src, size_t src_sz,
                            char *tgt, size_t tgt_sz,
                            char *fst, size_t fst_sz) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#') return 0;

    char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t len = (size_t)(p - start);
    if (len >= src_sz) len = src_sz - 1;
    memcpy(src, start, len);
    src[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 1;

    start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    len = (size_t)(p - start);
    if (len >= tgt_sz) len = tgt_sz - 1;
    memcpy(tgt, start, len);
    tgt[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 2;

    start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    len = (size_t)(p - start);
    if (len >= fst_sz) len = fst_sz - 1;
    memcpy(fst, start, len);
    fst[len] = '\0';

    return 3;
}

int main(int argc, char *argv[]) {
    int do_sync = 0;
    int first_file = -1;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--all") == 0) {
            opt_all = 1;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--human-readable") == 0) {
            opt_human = 1;
            opt_si = 0;
            continue;
        }
        if (strcmp(arg, "-H") == 0 || strcmp(arg, "--si") == 0) {
            opt_si = 1;
            opt_human = 0;
            continue;
        }
        if (strcmp(arg, "-i") == 0 || strcmp(arg, "--inodes") == 0) {
            opt_inodes = 1;
            continue;
        }
        if (strcmp(arg, "-k") == 0 || strcmp(arg, "--kilobytes") == 0) {
            opt_kilo = 1;
            opt_human = 0;
            opt_si = 0;
            opt_mega = 0;
            continue;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--local") == 0) {
            opt_local = 1;
            continue;
        }
        if (strcmp(arg, "-m") == 0 || strcmp(arg, "--megabytes") == 0) {
            opt_mega = 1;
            opt_human = 0;
            opt_si = 0;
            opt_kilo = 0;
            continue;
        }
        if (strcmp(arg, "-P") == 0 || strcmp(arg, "--portability") == 0) {
            opt_posix = 1;
            continue;
        }
        if (strcmp(arg, "-T") == 0 || strcmp(arg, "--print-type") == 0) {
            opt_print_type = 1;
            continue;
        }
        if (strcmp(arg, "--sync") == 0) {
            do_sync = 1;
            continue;
        }
        if (strcmp(arg, "--no-sync") == 0) {
            do_sync = 0;
            continue;
        }
        if (strncmp(arg, "--type=", 7) == 0) {
            opt_type = arg + 7;
            continue;
        }
        if (strcmp(arg, "-t") == 0 && i + 1 < argc) {
            opt_type = argv[++i];
            continue;
        }
        if (strncmp(arg, "--exclude-type=", 15) == 0) {
            opt_exclude = arg + 15;
            continue;
        }
        if (strcmp(arg, "-x") == 0 && i + 1 < argc) {
            opt_exclude = argv[++i];
            continue;
        }
        if (strcmp(arg, "-v") == 0) {
            /* System V compatibility: ignored */
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            first_file = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'a') opt_all = 1;
                else if (arg[c] == 'h') { opt_human = 1; opt_si = 0; }
                else if (arg[c] == 'H') { opt_si = 1; opt_human = 0; }
                else if (arg[c] == 'i') opt_inodes = 1;
                else if (arg[c] == 'k') { opt_kilo = 1; opt_human = 0; opt_si = 0; opt_mega = 0; }
                else if (arg[c] == 'l') opt_local = 1;
                else if (arg[c] == 'm') { opt_mega = 1; opt_human = 0; opt_si = 0; opt_kilo = 0; }
                else if (arg[c] == 'P') opt_posix = 1;
                else if (arg[c] == 'T') opt_print_type = 1;
                else if (arg[c] == 'v') { /* ignored */ }
                else {
                    fprintf(stderr, "df: invalid option -- '%c'\n", arg[c]);
                    print_usage();
                    return 1;
                }
            }
            continue;
        }
        first_file = i;
        break;
    }

    if (do_sync) {
        sync();
    }

    int fd = open("/proc/mounts", O_RDONLY);
    if (fd < 0) {
        perror("df: cannot open /proc/mounts");
        return 1;
    }

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0) return 0;
    buf[n] = '\0';

    /* Header formatting */
    if (opt_posix) {
        if (opt_inodes) {
            printf("%s %s %s %s %s %s\n",
                   "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
        } else {
            printf("%s %s %s %s %s %s\n",
                   "Filesystem", "1024-blocks", "Used", "Available", "Capacity", "Mounted on");
        }
    } else {
        if (opt_print_type) {
            if (opt_inodes) {
                printf("%-15s %-7s %10s %10s %10s %5s %s\n",
                       "Filesystem", "Type", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
            } else if (opt_human || opt_si) {
                printf("%-15s %-7s %8s %8s %8s %5s %s\n",
                       "Filesystem", "Type", "Size", "Used", "Avail", "Use%", "Mounted on");
            } else if (opt_mega) {
                printf("%-15s %-7s %10s %10s %10s %5s %s\n",
                       "Filesystem", "Type", "1M-blocks", "Used", "Available", "Use%", "Mounted on");
            } else {
                printf("%-15s %-7s %10s %10s %10s %5s %s\n",
                       "Filesystem", "Type", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
            }
        } else {
            if (opt_inodes) {
                printf("%-15s %10s %10s %10s %5s %s\n",
                       "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
            } else if (opt_human || opt_si) {
                printf("%-15s %8s %8s %8s %5s %s\n",
                       "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on");
            } else if (opt_mega) {
                printf("%-15s %10s %10s %10s %5s %s\n",
                       "Filesystem", "1M-blocks", "Used", "Available", "Use%", "Mounted on");
            } else {
                printf("%-15s %10s %10s %10s %5s %s\n",
                       "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
            }
        }
    }

    char *line = buf;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        if (*line) {
            char src[64] = {0}, tgt[128] = {0}, fst[32] = {0};
            if (parse_mount_line(line, src, sizeof(src), tgt, sizeof(tgt), fst, sizeof(fst)) >= 2) {
                /* Filter by type / exclude-type */
                if (opt_type && strcmp(fst, opt_type) != 0) goto next_line;
                if (opt_exclude && strcmp(fst, opt_exclude) == 0) goto next_line;

                /* Filter local */
                if (opt_local && strcmp(fst, "nfs") == 0) goto next_line;

                /* Filter specific FILE arguments if specified */
                if (first_file >= 0 && first_file < argc) {
                    int matched = 0;
                    for (int fi = first_file; fi < argc; ++fi) {
                        if (strcmp(argv[fi], tgt) == 0 || strcmp(argv[fi], src) == 0 ||
                            (argv[fi][0] == '/' && strncmp(argv[fi], tgt, strlen(tgt)) == 0)) {
                            matched = 1;
                            break;
                        }
                    }
                    if (!matched) goto next_line;
                }

                uint64_t total_bytes = 64ULL * 1024ULL * 1024ULL; /* 64 MB default */
                uint64_t used_bytes = 2560ULL * 1024ULL;          /* ~2.5 MB */
                uint64_t total_inodes = 65536ULL;
                uint64_t used_inodes = 240ULL;

                int is_pseudo = 0;
                if (strcmp(fst, "devfs") == 0 || strcmp(tgt, "/dev") == 0) {
                    total_bytes = 64ULL * 1024ULL;
                    used_bytes = 4ULL * 1024ULL;
                    total_inodes = 1024ULL;
                    used_inodes = 32ULL;
                    is_pseudo = 1;
                } else if (strcmp(fst, "procfs") == 0 || strcmp(tgt, "/proc") == 0) {
                    total_bytes = 64ULL * 1024ULL;
                    used_bytes = 4ULL * 1024ULL;
                    total_inodes = 1024ULL;
                    used_inodes = 40ULL;
                    is_pseudo = 1;
                }

                if (!opt_all && is_pseudo && (total_bytes == 0 || strcmp(fst, "procfs") == 0)) {
                    /* Omit non-standard pseudo filesystems unless -a is given */
                    goto next_line;
                }

                uint64_t avail_bytes = total_bytes > used_bytes ? (total_bytes - used_bytes) : 0;
                int pct = total_bytes > 0 ? (int)((used_bytes * 100ULL) / total_bytes) : 0;

                uint64_t free_inodes = total_inodes > used_inodes ? (total_inodes - used_inodes) : 0;
                int ipct = total_inodes > 0 ? (int)((used_inodes * 100ULL) / total_inodes) : 0;

                char s_tot[32], s_used[32], s_avail[32];
                if (opt_inodes) {
                    format_value(total_inodes, 0, s_tot, sizeof(s_tot));
                    format_value(used_inodes, 0, s_used, sizeof(s_used));
                    format_value(free_inodes, 0, s_avail, sizeof(s_avail));
                } else {
                    format_value(total_bytes, 1, s_tot, sizeof(s_tot));
                    format_value(used_bytes, 1, s_used, sizeof(s_used));
                    format_value(avail_bytes, 1, s_avail, sizeof(s_avail));
                }

                if (opt_posix) {
                    printf("%s %s %s %s %d%% %s\n",
                           src, s_tot, s_used, s_avail, opt_inodes ? ipct : pct, tgt);
                } else if (opt_print_type) {
                    if (opt_human || opt_si) {
                        printf("%-15s %-7s %8s %8s %8s %4d%% %s\n",
                               src, fst, s_tot, s_used, s_avail, opt_inodes ? ipct : pct, tgt);
                    } else {
                        printf("%-15s %-7s %10s %10s %10s %4d%% %s\n",
                               src, fst, s_tot, s_used, s_avail, opt_inodes ? ipct : pct, tgt);
                    }
                } else {
                    if (opt_human || opt_si) {
                        printf("%-15s %8s %8s %8s %4d%% %s\n",
                               src, s_tot, s_used, s_avail, opt_inodes ? ipct : pct, tgt);
                    } else {
                        printf("%-15s %10s %10s %10s %4d%% %s\n",
                               src, s_tot, s_used, s_avail, opt_inodes ? ipct : pct, tgt);
                    }
                }
            }
        }

next_line:
        if (!eol) break;
        line = eol + 1;
    }

    return 0;
}
