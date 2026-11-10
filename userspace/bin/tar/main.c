#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAR_BLOCK_SIZE 512

struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static uint8_t tar_block[TAR_BLOCK_SIZE];
static char path_buf[512];
static char dir_entry_buf[1024];

static int opt_create = 0;
static int opt_extract = 0;
static int opt_list = 0;
static int opt_verbose = 0;
static const char *archive_file = NULL;
static const char *chdir_path = NULL;

static void format_octal(char *dst, size_t size, uint64_t val) {
    if (size == 0) return;
    dst[size - 1] = '\0';
    if (size > 1) dst[size - 2] = ' ';
    for (int i = (int)size - 3; i >= 0; --i) {
        dst[i] = (char)('0' + (val & 7));
        val >>= 3;
    }
}

static uint64_t parse_octal(const char *src, size_t size) {
    uint64_t val = 0;
    while (size > 0 && (*src == ' ' || *src == '\0')) { src++; size--; }
    while (size > 0 && *src >= '0' && *src <= '7') {
        val = (val << 3) + (*src - '0');
        src++;
        size--;
    }
    return val;
}

static uint32_t calculate_checksum(const struct ustar_header *hdr) {
    const uint8_t *bytes = (const uint8_t *)hdr;
    uint32_t sum = 0;
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i) {
        if (i >= offsetof(struct ustar_header, chksum) &&
            i < offsetof(struct ustar_header, chksum) + 8) {
            sum += (uint8_t)' ';
        } else {
            sum += bytes[i];
        }
    }
    return sum;
}

static int write_header(int fd, const char *rel_path, const struct akaos_stat *st, char type) {
    memset(tar_block, 0, TAR_BLOCK_SIZE);
    struct ustar_header *hdr = (struct ustar_header *)tar_block;

    size_t nlen = strlen(rel_path);
    if (type == '5' && nlen > 0 && rel_path[nlen - 1] != '/') {
        snprintf(hdr->name, sizeof(hdr->name), "%s/", rel_path);
    } else {
        strncpy(hdr->name, rel_path, sizeof(hdr->name) - 1);
    }

    format_octal(hdr->mode, sizeof(hdr->mode), st->mode & 07777);
    format_octal(hdr->uid, sizeof(hdr->uid), st->uid);
    format_octal(hdr->gid, sizeof(hdr->gid), st->gid);
    format_octal(hdr->size, sizeof(hdr->size), type == '5' ? 0 : st->size);
    format_octal(hdr->mtime, sizeof(hdr->mtime), 0);

    hdr->typeflag = type;
    memcpy(hdr->magic, "ustar", 6);
    hdr->version[0] = '0';
    hdr->version[1] = '0';
    strncpy(hdr->uname, "root", sizeof(hdr->uname) - 1);
    strncpy(hdr->gname, "root", sizeof(hdr->gname) - 1);

    uint32_t csum = calculate_checksum(hdr);
    /* 6 octal digits + null + space */
    snprintf(hdr->chksum, sizeof(hdr->chksum), "%06o", csum);
    hdr->chksum[6] = '\0';
    hdr->chksum[7] = ' ';

    if (write(fd, tar_block, TAR_BLOCK_SIZE) != TAR_BLOCK_SIZE) return -1;
    return 0;
}

static int archive_entry(int tar_fd, const char *entry_path) {
    struct akaos_stat st;
    if (stat(entry_path, &st) != 0) {
        fprintf(stderr, "tar: %s: Cannot stat\n", entry_path);
        return -1;
    }

    if (opt_verbose) printf("%s\n", entry_path);

    if (S_ISDIR(st.mode)) {
        if (write_header(tar_fd, entry_path, &st, '5') < 0) return -1;

        int dfd = open(entry_path, O_RDONLY);
        if (dfd < 0) return -1;

        while (1) {
            ssize_t nread = getdents(dfd, (struct akaos_dirent *)dir_entry_buf, sizeof(dir_entry_buf));
            if (nread <= 0) break;

            int pos = 0;
            while (pos < (int)nread) {
                struct akaos_dirent *de = (struct akaos_dirent *)(dir_entry_buf + pos);
                if (pos + (int)sizeof(struct akaos_dirent) > (int)nread) break;

                if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0) {
                    snprintf(path_buf, sizeof(path_buf), "%s/%s", entry_path, de->name);
                    archive_entry(tar_fd, path_buf);
                }
                pos += (int)sizeof(struct akaos_dirent);
            }
        }
        close(dfd);
    } else {
        if (write_header(tar_fd, entry_path, &st, '0') < 0) return -1;

        int ffd = open(entry_path, O_RDONLY);
        if (ffd < 0) return -1;

        size_t remaining = (size_t)st.size;
        while (remaining > 0) {
            size_t chunk = remaining > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : remaining;
            memset(tar_block, 0, TAR_BLOCK_SIZE);
            ssize_t rd = read(ffd, tar_block, chunk);
            if (rd <= 0) break;
            if (write(tar_fd, tar_block, TAR_BLOCK_SIZE) != TAR_BLOCK_SIZE) {
                close(ffd);
                return -1;
            }
            remaining -= (size_t)rd;
        }
        close(ffd);
    }
    return 0;
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
            if (next < 0 && errno == ENOENT) {
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

int main(int argc, char **argv) {
    int arg_idx = 1;
    if (argc < 2) {
        fprintf(stderr, "tar: You must specify one of the '-Acdtrux' options\n");
        return 2;
    }

    /* Parse combined bundle options if first arg doesn't start with - */
    if (argv[1][0] != '-') {
        const char *bundle = argv[1];
        while (*bundle) {
            if (*bundle == 'c') opt_create = 1;
            else if (*bundle == 'x') opt_extract = 1;
            else if (*bundle == 't') opt_list = 1;
            else if (*bundle == 'v') opt_verbose = 1;
            else if (*bundle == 'f') {
                if (arg_idx + 1 < argc) {
                    archive_file = argv[++arg_idx];
                }
            }
            bundle++;
        }
        arg_idx++;
    }

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--create") == 0) {
            opt_create = 1;
            arg_idx++;
        } else if (strcmp(arg, "-x") == 0 || strcmp(arg, "--extract") == 0) {
            opt_extract = 1;
            arg_idx++;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--list") == 0) {
            opt_list = 1;
            arg_idx++;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
            arg_idx++;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0) {
            arg_idx++;
            if (arg_idx < argc) archive_file = argv[arg_idx++];
        } else if (strncmp(arg, "-f", 2) == 0 && arg[2]) {
            archive_file = arg + 2;
            arg_idx++;
        } else if (strcmp(arg, "-C") == 0 || strcmp(arg, "--directory") == 0) {
            arg_idx++;
            if (arg_idx < argc) chdir_path = argv[arg_idx++];
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: tar [OPTION...] [FILE]...\n"
                   "POSIX ustar tape archiver.\n\n"
                   "Main operation mode:\n"
                   "  -c, --create     create a new archive\n"
                   "  -x, --extract    extract files from an archive\n"
                   "  -t, --list       list the contents of an archive\n\n"
                   "Common options:\n"
                   "  -v, --verbose    verbosely list files processed\n"
                   "  -f, --file=ARCH  use archive file ARCH\n"
                   "  -C, --directory=DIR change to directory DIR\n"
                   "  -h, --help       display this help and exit\n");
            return 0;
        } else {
            /* Support combined short flags like -xvf */
            const char *f = arg + 1;
            int unknown = 0;
            while (*f) {
                if (*f == 'c') opt_create = 1;
                else if (*f == 'x') opt_extract = 1;
                else if (*f == 't') opt_list = 1;
                else if (*f == 'v') opt_verbose = 1;
                else if (*f == 'f') {
                    if (*(f + 1)) {
                        archive_file = f + 1;
                        break;
                    } else if (arg_idx + 1 < argc) {
                        archive_file = argv[++arg_idx];
                        break;
                    }
                } else {
                    unknown = 1;
                    break;
                }
                f++;
            }
            if (unknown) {
                fprintf(stderr, "tar: unrecognized option '%s'\n", arg);
                return 2;
            }
            arg_idx++;
        }
    }

    if (chdir_path) {
        if (chdir(chdir_path) != 0) {
            fprintf(stderr, "tar: Cannot change to directory %s\n", chdir_path);
            return 2;
        }
    }

    if (!opt_create && !opt_extract && !opt_list) {
        fprintf(stderr, "tar: You must specify one of the '-Acdtrux' options\n");
        return 2;
    }

    int tar_fd = -1;
    if (opt_create) {
        if (archive_file && strcmp(archive_file, "-") != 0) {
            tar_fd = open(archive_file, O_WRONLY | O_CREAT | O_TRUNC);
        } else {
            tar_fd = STDOUT_FILENO;
        }
        if (tar_fd < 0) {
            fprintf(stderr, "tar: Cannot open archive %s\n", archive_file ? archive_file : "stdout");
            return 2;
        }

        for (int i = arg_idx; i < argc; ++i) {
            archive_entry(tar_fd, argv[i]);
        }

        /* Write two zero blocks at end of archive */
        memset(tar_block, 0, TAR_BLOCK_SIZE);
        write(tar_fd, tar_block, TAR_BLOCK_SIZE);
        write(tar_fd, tar_block, TAR_BLOCK_SIZE);

        if (tar_fd != STDOUT_FILENO) close(tar_fd);
        return 0;
    }

    /* Extract or List */
    if (archive_file && strcmp(archive_file, "-") != 0) {
        tar_fd = open(archive_file, O_RDONLY);
    } else {
        tar_fd = STDIN_FILENO;
    }
    if (tar_fd < 0) {
        fprintf(stderr, "tar: Cannot open archive %s\n", archive_file ? archive_file : "stdin");
        return 2;
    }

    int root = -1, result = 0;
    if (opt_extract && !opt_list) {
        root = open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (root < 0) {
            if (tar_fd != STDIN_FILENO) close(tar_fd);
            return 2;
        }
    }
    int zero_blocks = 0;
    while (1) {
        ssize_t n = read(tar_fd, tar_block, TAR_BLOCK_SIZE);
        if (n != TAR_BLOCK_SIZE) break;

        /* Check for zero block */
        int is_zero = 1;
        for (int i = 0; i < TAR_BLOCK_SIZE; ++i) {
            if (tar_block[i] != 0) { is_zero = 0; break; }
        }
        if (is_zero) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        struct ustar_header *hdr = (struct ustar_header *)tar_block;
        char name[sizeof(hdr->name) + 1];
        memcpy(name, hdr->name, sizeof(hdr->name));
        name[sizeof(name) - 1] = '\0';
        uint64_t file_size = parse_octal(hdr->size, sizeof(hdr->size));
        char typeflag = hdr->typeflag ? hdr->typeflag : '0';

        if (opt_list) {
            if (opt_verbose) {
                uint64_t mode = parse_octal(hdr->mode, sizeof(hdr->mode));
                printf("%c%c%c%c%c%c%c%c%c %6lu %s\n",
                       typeflag == '5' ? 'd' : '-',
                       (mode & 0400) ? 'r' : '-', (mode & 0200) ? 'w' : '-', (mode & 0100) ? 'x' : '-',
                       (mode & 0040) ? 'r' : '-', (mode & 0020) ? 'w' : '-', (mode & 0010) ? 'x' : '-',
                       (mode & 0004) ? 'r' : '-', (mode & 0002) ? 'w' : '-', (mode & 0001) ? 'x' : '-',
                       (unsigned long)file_size, name);
            } else {
                printf("%s\n", name);
            }
        } else if (opt_extract) {
            if (opt_verbose) printf("%s\n", name);

            char path[sizeof(name)], *leaf = NULL;
            memcpy(path, name, sizeof(path));
            int parent = extraction_parent(root, path, &leaf);
            int out_fd = -1, error = 0;
            if (parent < 0) {
                error = 1;
            } else if (typeflag == '5') {
                if (mkdirat(parent, leaf, 0755) < 0) {
                    int dir = openat(parent, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
                    if (dir < 0) error = 1;
                    else close(dir);
                }
            } else {
                out_fd = openat(parent, leaf, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
                if (out_fd < 0) error = 1;
            }
            if (parent >= 0) close(parent);
            if (error) {
                fprintf(stderr, "tar: cannot extract %s\n", name);
                result = 1;
            }
            if (typeflag != '5') {
                if (out_fd >= 0) {
                    uint64_t rem = file_size;
                    while (rem > 0) {
                        uint8_t data_block[TAR_BLOCK_SIZE];
                        ssize_t rd = read(tar_fd, data_block, TAR_BLOCK_SIZE);
                        if (rd != TAR_BLOCK_SIZE) break;
                        size_t to_write = rem > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : (size_t)rem;
                        write(out_fd, data_block, to_write);
                        rem -= to_write;
                    }
                    close(out_fd);
                    continue; /* already read payload */
                }
            }
        }

        /* Skip payload blocks if not extracted */
        if (file_size > 0 && typeflag != '5') {
            size_t blocks_to_skip = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            for (size_t b = 0; b < blocks_to_skip; ++b) {
                read(tar_fd, tar_block, TAR_BLOCK_SIZE);
            }
        }
    }

    if (root >= 0) close(root);
    if (tar_fd != STDIN_FILENO) close(tar_fd);
    return result;
}
