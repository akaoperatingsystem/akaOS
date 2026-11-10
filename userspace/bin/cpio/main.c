#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define CPIO_VERSION "2.13 (akaOS coreutils)"

/* Magic constants */
#define CPIO_BIN_MAGIC 070707
#define CPIO_BIN_MAGIC_LE 0x71c7
#define CPIO_BIN_MAGIC_BE 0xc771

/* Old Binary Header: 26 bytes */
struct cpio_bin_header {
    uint16_t c_magic;
    uint16_t c_dev;
    uint16_t c_ino;
    uint16_t c_mode;
    uint16_t c_uid;
    uint16_t c_gid;
    uint16_t c_nlink;
    uint16_t c_rdev;
    uint16_t c_mtime[2];
    uint16_t c_namesize;
    uint16_t c_filesize[2];
};

/* Format enum */
enum cpio_format {
    FORMAT_AUTO = 0,
    FORMAT_BIN,
    FORMAT_ODC,
    FORMAT_NEWC,
    FORMAT_CRC,
    FORMAT_TAR,
    FORMAT_USTAR,
    FORMAT_HPBIN,
    FORMAT_HPODC
};

/* Operating modes */
enum cpio_mode {
    MODE_NONE = 0,
    MODE_IN,   /* -i, --extract */
    MODE_OUT,  /* -o, --create */
    MODE_PASS  /* -p, --pass-through */
};

static enum cpio_mode g_mode = MODE_NONE;
static enum cpio_format g_format = FORMAT_BIN; /* Default out format is bin */
static int opt_null = 0;           /* -0, --null */
static int opt_reset_atime = 0;    /* -a, --reset-access-time */
static int opt_append = 0;        /* -A, --append */
static int opt_swap_bytes = 0;     /* -s, --swap-bytes */
static int opt_swap_halfwords = 0; /* -S, --swap-halfwords */
static int opt_block_size = 512;   /* Default block size: 512 */
static int opt_make_dirs = 0;      /* -d, --make-directories */
static int opt_nonmatching = 0;    /* -f, --nonmatching */
static const char *opt_archive = NULL; /* -F, -I, -O */
static int opt_dereference = 0;    /* -L, --dereference */
static int opt_preserve_mtime = 0; /* -m, --preserve-modification-time */
static int opt_numeric_uid_gid = 0;/* -n, --numeric-uid-gid */



static int opt_quiet = 0;          /* --quiet */
static int opt_list = 0;           /* -t, --list */
static int opt_to_stdout = 0;      /* --to-stdout */
static int opt_unconditional = 0;  /* -u, --unconditional */
static int opt_verbose = 0;        /* -v, --verbose */
static int opt_dot = 0;            /* -V, --dot */
static int opt_link = 0;           /* -l, --link */
static int opt_sparse = 0;         /* --sparse */

static const char *g_dest_dir = NULL;
static char **g_patterns = NULL;
static int g_pattern_count = 0;
static uint64_t g_total_bytes = 0;

static void print_version(void) {
    printf("cpio %s\n", CPIO_VERSION);
}

static void print_usage(void) {
    printf("Usage: cpio {-o|--create} [options] < name-list [> archive]\n"
           "       cpio {-i|--extract} [options] [pattern...] [< archive]\n"
           "       cpio {-p|--pass-through} [options] destination-directory < name-list\n\n"
           "Main operation mode:\n"
           "  -i, --extract                extract files from an archive (or list if -t)\n"
           "  -o, --create                 create an archive\n"
           "  -p, --pass-through           copy files directly to destination-directory\n\n"
           "Operation modifiers:\n"
           "  -0, --null                   read null-terminated filenames\n"
           "  -a, --reset-access-time      reset access times after reading\n"
           "  -A, --append                 append to an existing archive\n"
           "  -b, --swap                   swap words and bytes\n"
           "  -B                           set I/O block size to 5120 bytes\n"
           "  -c                           identical to '-H newc'\n"
           "  -C, --io-size=BYTES          set I/O block size to BYTES\n"
           "  -d, --make-directories       create leading directories where needed\n"
           "  -f, --nonmatching            only copy files that do not match patterns\n"
           "  -F, -I, -O ARCHIVE           use archive file instead of stdin/stdout\n"
           "  -H, --format=FORMAT          archive format: bin, odc, newc, crc, tar, ustar\n"
           "  -l, --link                   link files instead of copying when possible\n"
           "  -L, --dereference            follow symbolic links\n"
           "  -m, --preserve-modification-time retain previous modification times\n"
           "  -n, --numeric-uid-gid        show numeric UID/GID in list mode\n"
           "  -t, --list                   list contents of the archive\n"
           "  -u, --unconditional          replace existing files unconditionally\n"
           "  -v, --verbose                list files processed or verbose table of contents\n"
           "  -V, --dot                    print a '.' for each file processed\n"
           "      --quiet                  do not print number of blocks copied\n"
           "      --to-stdout              extract files to standard output\n"
           "      --sparse                 write sparse files\n"
           "      --help                   display this help and exit\n"
           "      --version                output version information and exit\n");
}

/* Pattern matching: wildcard * and ? */
static int match_pattern(const char *pattern, const char *string) {
    if (!pattern || !string) return 0;
    while (*pattern) {
        if (*pattern == '*') {
            while (*(pattern + 1) == '*') pattern++;
            if (*(pattern + 1) == '\0') return 1;
            while (*string) {
                if (match_pattern(pattern + 1, string)) return 1;
                string++;
            }
            return 0;
        } else if (*pattern == '?' || *pattern == *string) {
            if (*string == '\0') return 0;
            pattern++;
            string++;
        } else {
            return 0;
        }
    }
    return (*string == '\0');
}

static int matches_any_pattern(const char *name) {
    if (g_pattern_count == 0) return !opt_nonmatching;
    for (int i = 0; i < g_pattern_count; ++i) {
        if (match_pattern(g_patterns[i], name)) {
            return !opt_nonmatching;
        }
    }
    return opt_nonmatching;
}

/* Ensure directory hierarchy exists */
static int make_parents(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return 0;
    *slash = '\0';

    char cur[512] = "";
    char *p = tmp;
    if (*p == '/') {
        cur[0] = '/';
        cur[1] = '\0';
        p++;
    }

    char *token = strtok(p, "/");
    while (token) {
        if (cur[0] && cur[strlen(cur) - 1] != '/') {
            strncat(cur, "/", sizeof(cur) - strlen(cur) - 1);
        }
        strncat(cur, token, sizeof(cur) - strlen(cur) - 1);

        struct stat st;
        if (stat(cur, &st) != 0) {
            if (mkdir(cur, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
        }
        token = strtok(NULL, "/");
    }
    return 0;
}

/* --- Copy-out (create) implementation --- */

static void write_archive_data(int fd, const void *buf, size_t size) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = size;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) {
            fprintf(stderr, "cpio: write error: %s\n", strerror(errno));
            exit(1);
        }
        p += n;
        left -= (size_t)n;
    }
    g_total_bytes += size;
}

static void write_padding(int fd, size_t count) {
    if (count == 0) return;
    static const uint8_t zeros[4] = {0, 0, 0, 0};
    write_archive_data(fd, zeros, count);
}

/* Create newc / SVR4 format header */
static void write_newc_entry(int out_fd, const char *path, const struct stat *st, int fd) {
    char header[111];
    size_t name_len = strlen(path) + 1; /* includes null terminator */
    uint32_t filesize = S_ISREG(st->st_mode) ? (uint32_t)st->st_size : 0;

    snprintf(header, sizeof(header),
             "070701"        /* magic */
             "%08X"          /* ino */
             "%08X"          /* mode */
             "%08X"          /* uid */
             "%08X"          /* gid */
             "%08X"          /* nlink */
             "%08X"          /* mtime */
             "%08X"          /* filesize */
             "%08X"          /* devmajor */
             "%08X"          /* devminor */
             "%08X"          /* rdevmajor */
             "%08X"          /* rdevminor */
             "%08X"          /* namesize */
             "%08X",         /* check (0 for newc) */
             (uint32_t)st->st_ino,
             (uint32_t)st->st_mode,
             (uint32_t)st->st_uid,
             (uint32_t)st->st_gid,
             (uint32_t)st->st_nlink,
             (uint32_t)st->st_mtime,
             filesize,
             (uint32_t)major(st->st_dev),
             (uint32_t)minor(st->st_dev),
             (uint32_t)major(st->st_rdev),
             (uint32_t)minor(st->st_rdev),
             (uint32_t)name_len,
             0);

    write_archive_data(out_fd, header, 110);
    write_archive_data(out_fd, path, name_len);

    /* Pad header + name to 4 bytes boundary */
    size_t pad1 = (4 - ((110 + name_len) % 4)) % 4;
    write_padding(out_fd, pad1);

    if (filesize > 0 && fd >= 0) {
        char buf[4096];
        uint32_t left = filesize;
        while (left > 0) {
            size_t to_read = (left > sizeof(buf)) ? sizeof(buf) : left;
            ssize_t n = read(fd, buf, to_read);
            if (n <= 0) break;
            write_archive_data(out_fd, buf, (size_t)n);
            left -= (uint32_t)n;
        }
        /* Pad file data to 4 bytes */
        size_t pad2 = (4 - (filesize % 4)) % 4;
        write_padding(out_fd, pad2);
    }
}

/* Create binary format entry */
static void write_bin_entry(int out_fd, const char *path, const struct stat *st, int fd) {
    struct cpio_bin_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    size_t name_len = strlen(path) + 1;
    uint32_t filesize = S_ISREG(st->st_mode) ? (uint32_t)st->st_size : 0;

    hdr.c_magic = CPIO_BIN_MAGIC;
    hdr.c_dev = (uint16_t)st->st_dev;
    hdr.c_ino = (uint16_t)st->st_ino;
    hdr.c_mode = (uint16_t)st->st_mode;
    hdr.c_uid = (uint16_t)st->st_uid;
    hdr.c_gid = (uint16_t)st->st_gid;
    hdr.c_nlink = (uint16_t)st->st_nlink;
    hdr.c_rdev = (uint16_t)st->st_rdev;
    hdr.c_mtime[0] = (uint16_t)(st->st_mtime >> 16);
    hdr.c_mtime[1] = (uint16_t)(st->st_mtime & 0xFFFF);
    hdr.c_namesize = (uint16_t)name_len;
    hdr.c_filesize[0] = (uint16_t)(filesize >> 16);
    hdr.c_filesize[1] = (uint16_t)(filesize & 0xFFFF);

    write_archive_data(out_fd, &hdr, sizeof(hdr));
    write_archive_data(out_fd, path, name_len);

    /* Pad to 2-byte boundary */
    if (name_len % 2 != 0) {
        write_padding(out_fd, 1);
    }

    if (filesize > 0 && fd >= 0) {
        char buf[4096];
        uint32_t left = filesize;
        while (left > 0) {
            size_t to_read = (left > sizeof(buf)) ? sizeof(buf) : left;
            ssize_t n = read(fd, buf, to_read);
            if (n <= 0) break;
            write_archive_data(out_fd, buf, (size_t)n);
            left -= (uint32_t)n;
        }
        if (filesize % 2 != 0) {
            write_padding(out_fd, 1);
        }
    }
}

/* Write trailer */
static void write_trailer(int out_fd) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_nlink = 1;
    if (g_format == FORMAT_NEWC || g_format == FORMAT_CRC) {
        write_newc_entry(out_fd, "TRAILER!!!", &st, -1);
    } else {
        write_bin_entry(out_fd, "TRAILER!!!", &st, -1);
    }

    /* Pad entire archive to block size */
    size_t rem = g_total_bytes % (size_t)opt_block_size;
    if (rem != 0) {
        write_padding(out_fd, (size_t)opt_block_size - rem);
    }
}

static int do_copy_out(void) {
    int out_fd = STDOUT_FILENO;
    if (opt_archive) {
        int flags = O_WRONLY | O_CREAT | (opt_append ? 0 : O_TRUNC);
        out_fd = open(opt_archive, flags, 0644);
        if (out_fd < 0) {
            fprintf(stderr, "cpio: cannot open %s: %s\n", opt_archive, strerror(errno));
            return 1;
        }
        if (opt_append) {
            lseek(out_fd, 0, SEEK_END);
        }
    }

    char line[512];
    while (1) {
        size_t pos = 0;
        int c;
        while ((c = getchar()) != EOF) {
            if (opt_null) {
                if (c == 0) break;
            } else {
                if (c == '\n') break;
            }
            if (pos + 1 < sizeof(line)) {
                line[pos++] = (char)c;
            }
        }
        if (pos == 0 && c == EOF) break;
        line[pos] = '\0';

        if (pos == 0) continue;

        struct stat st;
        if (stat(line, &st) != 0) {
            fprintf(stderr, "cpio: %s: %s\n", line, strerror(errno));
            continue;
        }

        int in_fd = -1;
        if (S_ISREG(st.st_mode)) {
            in_fd = open(line, O_RDONLY);
            if (in_fd < 0) {
                fprintf(stderr, "cpio: cannot open %s: %s\n", line, strerror(errno));
                continue;
            }
        }

        if (g_format == FORMAT_NEWC || g_format == FORMAT_CRC) {
            write_newc_entry(out_fd, line, &st, in_fd);
        } else {
            write_bin_entry(out_fd, line, &st, in_fd);
        }

        if (in_fd >= 0) close(in_fd);

        if (opt_verbose) {
            printf("%s\n", line);
        } else if (opt_dot) {
            putchar('.');
            fflush(stdout);
        }
    }

    write_trailer(out_fd);

    if (opt_archive && out_fd >= 0) {
        close(out_fd);
    }

    if (!opt_quiet) {
        uint64_t blocks = (g_total_bytes + 511) / 512;
        fprintf(stderr, "%llu blocks\n", (unsigned long long)blocks);
    }

    return 0;
}

/* --- Copy-in (extract / list) implementation --- */

static ssize_t read_all(int fd, void *buf, size_t size) {
    uint8_t *p = (uint8_t *)buf;
    size_t left = size;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) return -1;
        if (n == 0) break;
        p += n;
        left -= (size_t)n;
    }
    return (ssize_t)(size - left);
}

static void skip_bytes(int fd, size_t count) {
    char buf[4096];
    size_t left = count;
    while (left > 0) {
        size_t to_read = (left > sizeof(buf)) ? sizeof(buf) : left;
        ssize_t n = read_all(fd, buf, to_read);
        if (n <= 0) break;
        left -= (size_t)n;
    }
}

/* Resolve each parent beneath the extraction root without following links.
 * Reject all parent components, including ones that would stay inside the root.
 */
static int extraction_parent(int root, char *path, char **leaf) {
    if (!*path || *path == '/') { errno = EINVAL; return -1; }
    for (char *p = path; *p;) {
        char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            errno = EINVAL;
            return -1;
        }
        p += len;
        if (*p) ++p;
    }

    int parent = openat(root, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) return -1;
    char *p = path;
    for (;;) {
        char *slash = strchr(p, '/');
        if (!slash) { *leaf = *p ? p : "."; return parent; }
        *slash = '\0';
        if (*p && strcmp(p, ".") != 0) {
            int next = openat(parent, p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (next < 0 && errno == ENOENT && opt_make_dirs) {
                if (mkdirat(parent, p, 0755) == 0 || errno == EEXIST)
                    next = openat(parent, p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            }
            if (next < 0) {
                int error = errno;
                close(parent);
                errno = error;
                return -1;
            }
            close(parent);
            parent = next;
        }
        p = slash + 1;
    }
}

/* Consume exactly one member, even if its destination is rejected. */
static int extract_member(int in_fd, int root, const char *name,
                          uint32_t mode, uint32_t filesize, uint32_t mtime) {
    int out = -1, parent = -1, result = 0;
    char *path = NULL, *leaf = NULL;
    if (opt_to_stdout) {
        if (!S_ISDIR(mode)) out = STDOUT_FILENO;
    } else {
        path = strdup(name);
        if (path) parent = extraction_parent(root, path, &leaf);
        if (parent < 0) {
            result = -1;
        } else if (S_ISDIR(mode)) {
            if (mkdirat(parent, leaf, mode & 07777) < 0) {
                /* An existing directory is acceptable, but never a symlink. */
                int dir = openat(parent, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
                if (dir < 0) result = -1;
                else close(dir);
            }
        } else {
            out = openat(parent, leaf, O_WRONLY | O_CREAT | O_NOFOLLOW |
                         (opt_unconditional ? O_TRUNC : O_EXCL), mode & 07777);
            if (out < 0) result = -1;
        }
        if (result < 0)
            fprintf(stderr, "cpio: cannot extract %s: %s\n", name, strerror(errno));
    }

    char buf[4096];
    uint32_t left = filesize;
    while (left > 0) {
        size_t count = left > sizeof(buf) ? sizeof(buf) : left;
        ssize_t nr = read_all(in_fd, buf, count);
        if (nr <= 0) { result = -1; break; }
        if (out >= 0) write(out, buf, (size_t)nr);
        left -= (uint32_t)nr;
    }
    if (out >= 0 && out != STDOUT_FILENO) {
        if (opt_preserve_mtime) {
            struct timespec times[2] = {{mtime, 0}, {mtime, 0}};
            futimens(out, times);
        }
        close(out);
    }
    if (parent >= 0) close(parent);
    free(path);
    return result;
}

static int do_copy_in(void) {
    int in_fd = STDIN_FILENO;
    if (opt_archive) {
        in_fd = open(opt_archive, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "cpio: cannot open %s: %s\n", opt_archive, strerror(errno));
            return 1;
        }
    }

    int result = 0;
    int root = -1;
    if (!opt_list && !opt_to_stdout) {
        root = open(".", O_RDONLY | O_DIRECTORY);
        if (root < 0) {
            if (opt_archive) close(in_fd);
            return 1;
        }
    }
    uint64_t bytes_read = 0;

    for (;;) {
        uint8_t magic_buf[6];
        ssize_t n = read_all(in_fd, magic_buf, 2);
        if (n < 2) break;
        bytes_read += 2;

        /* Check for newc / crc ASCII magic */
        if (magic_buf[0] == '0' && magic_buf[1] == '7') {
            /* Read next 4 bytes of magic */
            ssize_t n2 = read_all(in_fd, magic_buf + 2, 4);
            if (n2 < 4) break;
            bytes_read += 4;

            if (memcmp(magic_buf, "070701", 6) == 0 || memcmp(magic_buf, "070702", 6) == 0) {
                /* SVR4 newc or crc format */
                char hdr_fields[105]; /* 110 - 6 = 104 bytes */
                if (read_all(in_fd, hdr_fields, 104) < 104) break;
                bytes_read += 104;
                hdr_fields[104] = '\0';

                uint32_t ino, mode, uid, gid, nlink, mtime, filesize, maj, min, rmaj, rmin, namesize, chk;
                sscanf(hdr_fields, "%8x%8x%8x%8x%8x%8x%8x%8x%8x%8x%8x%8x%8x",
                       &ino, &mode, &uid, &gid, &nlink, &mtime, &filesize,
                       &maj, &min, &rmaj, &rmin, &namesize, &chk);

                char *name = malloc(namesize + 1);
                if (read_all(in_fd, name, namesize) < (ssize_t)namesize) {
                    free(name);
                    break;
                }
                bytes_read += namesize;
                name[namesize] = '\0';

                /* Pad name to 4 bytes */
                size_t pad1 = (4 - ((110 + namesize) % 4)) % 4;
                skip_bytes(in_fd, pad1);
                bytes_read += pad1;

                if (strcmp(name, "TRAILER!!!") == 0) {
                    free(name);
                    break;
                }

                int selected = matches_any_pattern(name);

                if (opt_list) {
                    if (selected) {
                        if (opt_verbose) {
                            char time_str[32] = "";
                            time_t t = (time_t)mtime;
                            struct tm *tm_info = localtime(&t);
                            if (tm_info) strftime(time_str, sizeof(time_str), "%b %e %H:%M", tm_info);
                            printf("%06o %3u %-8u %-8u %8u %s %s\n",
                                   mode, nlink, uid, gid, filesize, time_str, name);
                        } else {
                            printf("%s\n", name);
                        }
                    }
                    skip_bytes(in_fd, filesize);
                    bytes_read += filesize;
                } else if (selected) {
                    if (extract_member(in_fd, root, name, mode, filesize, mtime) < 0)
                        result = 1;
                    bytes_read += filesize;

                    if (opt_verbose) {
                        printf("%s\n", name);
                    } else if (opt_dot) {
                        putchar('.');
                        fflush(stdout);
                    }
                } else {
                    skip_bytes(in_fd, filesize);
                    bytes_read += filesize;
                }

                /* Pad file to 4 bytes */
                size_t pad2 = (4 - (filesize % 4)) % 4;
                skip_bytes(in_fd, pad2);
                bytes_read += pad2;

                free(name);
                continue;
            }
        }

        /* Check binary format */
        uint16_t bmagic = (uint16_t)(magic_buf[0] | (magic_buf[1] << 8));
        if (bmagic == CPIO_BIN_MAGIC || bmagic == CPIO_BIN_MAGIC_LE || bmagic == CPIO_BIN_MAGIC_BE) {
            struct cpio_bin_header hdr;
            hdr.c_magic = bmagic;
            if (read_all(in_fd, ((uint8_t *)&hdr) + 2, sizeof(hdr) - 2) < (ssize_t)(sizeof(hdr) - 2)) {
                break;
            }
            bytes_read += sizeof(hdr) - 2;

            size_t namesize = hdr.c_namesize;
            uint32_t filesize = ((uint32_t)hdr.c_filesize[0] << 16) | hdr.c_filesize[1];
            uint32_t mtime = ((uint32_t)hdr.c_mtime[0] << 16) | hdr.c_mtime[1];

            char *name = malloc(namesize + 1);
            if (read_all(in_fd, name, namesize) < (ssize_t)namesize) {
                free(name);
                break;
            }
            bytes_read += namesize;
            name[namesize] = '\0';

            if (namesize % 2 != 0) {
                skip_bytes(in_fd, 1);
                bytes_read += 1;
            }

            if (strcmp(name, "TRAILER!!!") == 0) {
                free(name);
                break;
            }

            int selected = matches_any_pattern(name);

            if (opt_list) {
                if (selected) {
                    if (opt_verbose) {
                        printf("%06o %3u %-8u %-8u %8u %s\n",
                               hdr.c_mode, hdr.c_nlink, hdr.c_uid, hdr.c_gid, filesize, name);
                    } else {
                        printf("%s\n", name);
                    }
                }
                skip_bytes(in_fd, filesize);
                bytes_read += filesize;
            } else if (selected) {
                if (extract_member(in_fd, root, name, hdr.c_mode, filesize, mtime) < 0)
                    result = 1;
                bytes_read += filesize;

                if (opt_verbose) {
                    printf("%s\n", name);
                } else if (opt_dot) {
                    putchar('.');
                    fflush(stdout);
                }
            } else {
                skip_bytes(in_fd, filesize);
                bytes_read += filesize;
            }

            if (filesize % 2 != 0) {
                skip_bytes(in_fd, 1);
                bytes_read += 1;
            }

            free(name);
            continue;
        }

        /* Unknown or padding */
        break;
    }

    if (opt_archive && in_fd >= 0) {
        close(in_fd);
    }

    if (!opt_quiet) {
        uint64_t blocks = (bytes_read + 511) / 512;
        fprintf(stderr, "%llu blocks\n", (unsigned long long)blocks);
    }

    if (root >= 0) close(root);
    return result;
}

/* --- Pass-through mode implementation --- */

static int do_pass_through(void) {
    if (!g_dest_dir) {
        fprintf(stderr, "cpio: destination directory required in pass-through mode\n");
        return 1;
    }

    struct stat dest_st;
    if (stat(g_dest_dir, &dest_st) != 0 || !S_ISDIR(dest_st.st_mode)) {
        fprintf(stderr, "cpio: %s: Not a directory\n", g_dest_dir);
        return 1;
    }

    char line[512];
    while (1) {
        size_t pos = 0;
        int c;
        while ((c = getchar()) != EOF) {
            if (opt_null) {
                if (c == 0) break;
            } else {
                if (c == '\n') break;
            }
            if (pos + 1 < sizeof(line)) {
                line[pos++] = (char)c;
            }
        }
        if (pos == 0 && c == EOF) break;
        line[pos] = '\0';
        if (pos == 0) continue;

        char target_path[1024];
        if (strcmp(g_dest_dir, "/") == 0) {
            snprintf(target_path, sizeof(target_path), "/%s", line);
        } else {
            snprintf(target_path, sizeof(target_path), "%s/%s", g_dest_dir, line);
        }

        struct stat st;
        if (stat(line, &st) != 0) {
            fprintf(stderr, "cpio: %s: %s\n", line, strerror(errno));
            continue;
        }

        if (opt_make_dirs) {
            make_parents(target_path);
        }

        if (S_ISDIR(st.st_mode)) {
            mkdir(target_path, st.st_mode & 07777);
        } else if (opt_link && link(line, target_path) == 0) {
            /* Link successful */
        } else {
            int in_f = open(line, O_RDONLY);
            if (in_f < 0) {
                fprintf(stderr, "cpio: cannot open %s: %s\n", line, strerror(errno));
                continue;
            }
            int out_f = open(target_path, O_WRONLY | O_CREAT | (opt_unconditional ? O_TRUNC : O_EXCL), st.st_mode & 07777);
            if (out_f < 0 && errno == EEXIST && opt_unconditional) {
                unlink(target_path);
                out_f = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
            }
            if (out_f >= 0) {
                char buf[4096];
                ssize_t nr;
                while ((nr = read(in_f, buf, sizeof(buf))) > 0) {
                    write(out_f, buf, (size_t)nr);
                }
                close(out_f);
                if (opt_preserve_mtime) {
                    struct utimbuf ut;
                    ut.actime = st.st_atime;
                    ut.modtime = st.st_mtime;
                    utime(target_path, &ut);
                }
            } else {
                fprintf(stderr, "cpio: cannot create %s: %s\n", target_path, strerror(errno));
            }
            close(in_f);
        }

        if (opt_verbose) {
            printf("%s\n", target_path);
        } else if (opt_dot) {
            putchar('.');
            fflush(stdout);
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

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
        if (strcmp(arg, "-o") == 0 || strcmp(arg, "--create") == 0) {
            g_mode = MODE_OUT;
            continue;
        }
        if (strcmp(arg, "-i") == 0 || strcmp(arg, "--extract") == 0) {
            g_mode = MODE_IN;
            continue;
        }
        if (strcmp(arg, "-p") == 0 || strcmp(arg, "--pass-through") == 0) {
            g_mode = MODE_PASS;
            continue;
        }
        if (strcmp(arg, "-0") == 0 || strcmp(arg, "--null") == 0) {
            opt_null = 1;
            continue;
        }
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--reset-access-time") == 0) {
            opt_reset_atime = 1;
            continue;
        }
        if (strcmp(arg, "-A") == 0 || strcmp(arg, "--append") == 0) {
            opt_append = 1;
            continue;
        }
        if (strcmp(arg, "-b") == 0 || strcmp(arg, "--swap") == 0) {
            opt_swap_bytes = 1;
            opt_swap_halfwords = 1;
            continue;
        }
        if (strcmp(arg, "-B") == 0) {
            opt_block_size = 5120;
            continue;
        }
        if (strcmp(arg, "-c") == 0) {
            g_format = FORMAT_NEWC;
            continue;
        }
        if (strncmp(arg, "--block-size=", 13) == 0) {
            opt_block_size = atoi(arg + 13) * 512;
            continue;
        }
        if (strncmp(arg, "-C", 2) == 0) {
            if (arg[2] != '\0') opt_block_size = atoi(arg + 2);
            else if (i + 1 < argc) opt_block_size = atoi(argv[++i]);
            continue;
        }
        if (strncmp(arg, "--io-size=", 10) == 0) {
            opt_block_size = atoi(arg + 10);
            continue;
        }
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--make-directories") == 0) {
            opt_make_dirs = 1;
            continue;
        }
        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--nonmatching") == 0) {
            opt_nonmatching = 1;
            continue;
        }
        if (strcmp(arg, "-F") == 0 || strcmp(arg, "-I") == 0 || strcmp(arg, "-O") == 0) {
            if (i + 1 < argc) opt_archive = argv[++i];
            continue;
        }
        if (strncmp(arg, "--file=", 7) == 0) {
            opt_archive = arg + 7;
            continue;
        }
        if (strncmp(arg, "-H", 2) == 0) {
            const char *fmt = (arg[2] != '\0') ? arg + 2 : (i + 1 < argc ? argv[++i] : "bin");
            if (strcasecmp(fmt, "newc") == 0) g_format = FORMAT_NEWC;
            else if (strcasecmp(fmt, "crc") == 0) g_format = FORMAT_CRC;
            else if (strcasecmp(fmt, "odc") == 0) g_format = FORMAT_ODC;
            else if (strcasecmp(fmt, "tar") == 0 || strcasecmp(fmt, "ustar") == 0) g_format = FORMAT_TAR;
            else g_format = FORMAT_BIN;
            continue;
        }
        if (strncmp(arg, "--format=", 9) == 0) {
            const char *fmt = arg + 9;
            if (strcasecmp(fmt, "newc") == 0) g_format = FORMAT_NEWC;
            else if (strcasecmp(fmt, "crc") == 0) g_format = FORMAT_CRC;
            else if (strcasecmp(fmt, "odc") == 0) g_format = FORMAT_ODC;
            else if (strcasecmp(fmt, "tar") == 0 || strcasecmp(fmt, "ustar") == 0) g_format = FORMAT_TAR;
            else g_format = FORMAT_BIN;
            continue;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--link") == 0) {
            opt_link = 1;
            continue;
        }
        if (strcmp(arg, "-L") == 0 || strcmp(arg, "--dereference") == 0) {
            opt_dereference = 1;
            continue;
        }
        if (strcmp(arg, "-m") == 0 || strcmp(arg, "--preserve-modification-time") == 0) {
            opt_preserve_mtime = 1;
            continue;
        }
        if (strcmp(arg, "-n") == 0 || strcmp(arg, "--numeric-uid-gid") == 0) {
            opt_numeric_uid_gid = 1;
            continue;
        }
        if (strcmp(arg, "--quiet") == 0) {
            opt_quiet = 1;
            continue;
        }
        if (strcmp(arg, "-t") == 0 || strcmp(arg, "--list") == 0) {
            opt_list = 1;
            continue;
        }
        if (strcmp(arg, "--to-stdout") == 0) {
            opt_to_stdout = 1;
            continue;
        }
        if (strcmp(arg, "-u") == 0 || strcmp(arg, "--unconditional") == 0) {
            opt_unconditional = 1;
            continue;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
            continue;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--dot") == 0) {
            opt_dot = 1;
            continue;
        }
        if (strcmp(arg, "--sparse") == 0) {
            opt_sparse = 1;
            continue;
        }

        /* Combined short options like -ov, -iv, -idv, -pmud, -tv */
        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'o') g_mode = MODE_OUT;
                else if (arg[c] == 'i') g_mode = MODE_IN;
                else if (arg[c] == 'p') g_mode = MODE_PASS;
                else if (arg[c] == 'v') opt_verbose = 1;
                else if (arg[c] == 'V') opt_dot = 1;
                else if (arg[c] == 'd') opt_make_dirs = 1;
                else if (arg[c] == 'm') opt_preserve_mtime = 1;
                else if (arg[c] == 'u') opt_unconditional = 1;
                else if (arg[c] == 't') opt_list = 1;
                else if (arg[c] == 'c') g_format = FORMAT_NEWC;
                else if (arg[c] == 'B') opt_block_size = 5120;
                else if (arg[c] == 'l') opt_link = 1;
                else if (arg[c] == 'L') opt_dereference = 1;
                else if (arg[c] == 'n') opt_numeric_uid_gid = 1;
                else if (arg[c] == 's') opt_swap_bytes = 1;
                else if (arg[c] == 'S') opt_swap_halfwords = 1;
                else if (arg[c] == '0') opt_null = 1;
                else if (arg[c] == 'a') opt_reset_atime = 1;
                else if (arg[c] == 'A') opt_append = 1;
                else if (arg[c] == 'f') opt_nonmatching = 1;
            }
            continue;
        }

        /* Non-option argument */
        if (g_mode == MODE_PASS && !g_dest_dir) {
            g_dest_dir = arg;
        } else {
            /* Pattern in copy-in mode */
            if (!g_patterns) g_patterns = malloc(sizeof(char *) * 32);
            g_patterns[g_pattern_count++] = arg;
        }
    }

    if (g_mode == MODE_NONE) {
        if (opt_list) {
            g_mode = MODE_IN;
        } else {
            fprintf(stderr, "cpio: You must specify one of -o, -i, or -p, but you didn't.\n"
                            "Try 'cpio --help' for more information.\n");
            return 1;
        }
    }

    if (g_mode == MODE_OUT) {
        return do_copy_out();
    } else if (g_mode == MODE_IN) {
        return do_copy_in();
    } else if (g_mode == MODE_PASS) {
        return do_pass_through();
    }

    return 0;
}
