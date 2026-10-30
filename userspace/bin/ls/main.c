#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

static void print_error(const char *target, const char *msg) {
    write_str(STDERR_FILENO, "ls: cannot access '");
    write_str(STDERR_FILENO, target);
    write_str(STDERR_FILENO, "': ");
    write_str(STDERR_FILENO, msg);
    write_str(STDERR_FILENO, "\n");
}

/* Keep number formatting freestanding on 32-bit targets.  The compiler's
 * normal uint64_t division helper (__aeabi_uldivmod on ARM32) is not part of
 * the tiny userspace runtime, so divide by ten with a small restoring divide
 * instead. */
static uint64_t divide_u64_by_ten(uint64_t value, unsigned *remainder) {
    uint64_t quotient = 0;
    uint64_t partial = 0;
    for (int bit = 63; bit >= 0; --bit) {
        partial = (partial << 1) | ((value >> (unsigned)bit) & 1U);
        if (partial >= 10U) {
            partial -= 10U;
            quotient |= ((uint64_t)1 << (unsigned)bit);
        }
    }
    if (remainder) {
        *remainder = (unsigned)partial;
    }
    return quotient;
}

static void format_num(uint64_t val, char *buf, size_t buf_size) {
    char temp[32];
    size_t count = 0;
    if (val == 0) {
        temp[count++] = '0';
    } else {
        while (val > 0 && count < sizeof(temp)) {
            unsigned remainder = 0;
            val = divide_u64_by_ten(val, &remainder);
            temp[count++] = (char)('0' + remainder);
        }
    }
    size_t out = 0;
    while (count > 0 && out + 1 < buf_size) {
        buf[out++] = temp[--count];
    }
    buf[out] = '\0';
}

static void print_long_entry(const struct akaos_stat *st, const char *name) {
    char perm[12];
    perm[0] = st->type == AKAOS_DT_DIRECTORY
                  ? 'd'
                  : (st->type == AKAOS_DT_CHARACTER ? 'c' : '-');
    perm[1] = (st->mode & 0400) ? 'r' : '-';
    perm[2] = (st->mode & 0200) ? 'w' : '-';
    perm[3] = (st->mode & 0100) ? ((st->mode & 04000) ? 's' : 'x') : ((st->mode & 04000) ? 'S' : '-');
    perm[4] = (st->mode & 0040) ? 'r' : '-';
    perm[5] = (st->mode & 0020) ? 'w' : '-';
    perm[6] = (st->mode & 0010) ? ((st->mode & 02000) ? 's' : 'x') : ((st->mode & 02000) ? 'S' : '-');
    perm[7] = (st->mode & 0004) ? 'r' : '-';
    perm[8] = (st->mode & 0002) ? 'w' : '-';
    perm[9] = (st->mode & 0001) ? ((st->mode & 01000) ? 't' : 'x') : ((st->mode & 01000) ? 'T' : '-');
    perm[10] = '\0';

    char size_buf[32];
    format_num(st->size, size_buf, sizeof(size_buf));

    write_str(STDOUT_FILENO, perm);
    write_str(STDOUT_FILENO, " 1 ");
    if (st->uid == 0) write_str(STDOUT_FILENO, "root ");
    else {
        char ubuf[16];
        format_num(st->uid, ubuf, sizeof(ubuf));
        write_str(STDOUT_FILENO, ubuf);
        write_str(STDOUT_FILENO, " ");
    }
    if (st->gid == 0) write_str(STDOUT_FILENO, "root ");
    else {
        char gbuf[16];
        format_num(st->gid, gbuf, sizeof(gbuf));
        write_str(STDOUT_FILENO, gbuf);
        write_str(STDOUT_FILENO, " ");
    }
    /* Right-align size to 6 spaces */
    size_t slen = strlen(size_buf);
    for (size_t i = slen; i < 6; ++i) {
        write_str(STDOUT_FILENO, " ");
    }
    write_str(STDOUT_FILENO, size_buf);
    write_str(STDOUT_FILENO, " ");
    write_str(STDOUT_FILENO, name);
    write_str(STDOUT_FILENO, "\n");
}

static int list_path(const char *path, int opt_a, int opt_l, int print_header) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EACCES) {
            print_error(path, "Permission denied");
        } else if (errno == ENOTDIR) {
            print_error(path, "Not a directory");
        } else {
            print_error(path, "Error accessing file");
        }
        return 1;
    }

    if (print_header) {
        write_str(STDOUT_FILENO, path);
        write_str(STDOUT_FILENO, ":\n");
    }

    if (st.type != AKAOS_DT_DIRECTORY) {
        if (opt_l) {
            print_long_entry(&st, path);
        } else {
            write_str(STDOUT_FILENO, path);
            write_str(STDOUT_FILENO, "\n");
        }
        return 0;
    }

    /* Directory */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print_error(path, "Cannot open directory");
        return 1;
    }

    struct akaos_dirent entries[16];
    for (;;) {
        ssize_t count = getdents(fd, entries, 16);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (!name || name[0] == '\0') {
                continue;
            }
            if (!opt_a && name[0] == '.') {
                continue;
            }
            if (opt_l) {
                char subpath[128];
                size_t plen = strlen(path);
                size_t nlen = strlen(name);
                if (strcmp(path, ".") == 0) {
                    size_t copy = nlen < sizeof(subpath) - 1 ? nlen : sizeof(subpath) - 1;
                    memcpy(subpath, name, copy);
                    subpath[copy] = '\0';
                } else if (strcmp(path, "/") == 0) {
                    subpath[0] = '/';
                    size_t copy = nlen < sizeof(subpath) - 2 ? nlen : sizeof(subpath) - 2;
                    memcpy(subpath + 1, name, copy);
                    subpath[copy + 1] = '\0';
                } else {
                    if (plen + 1 + nlen < sizeof(subpath)) {
                        memcpy(subpath, path, plen);
                        subpath[plen] = '/';
                        memcpy(subpath + plen + 1, name, nlen);
                        subpath[plen + 1 + nlen] = '\0';
                    } else {
                        /* The VFS path is too long to stat safely.  Leave an
                         * empty path so the fallback metadata below is used
                         * without reading an unterminated buffer. */
                        subpath[0] = '\0';
                    }
                }
                struct akaos_stat sub_st;
                if (stat(subpath, &sub_st) == 0) {
                    print_long_entry(&sub_st, name);
                } else {
                    struct akaos_stat fallback;
                    fallback.type = entries[i].type;
                    fallback.mode = entries[i].type == AKAOS_DT_DIRECTORY
                                        ? 0755
                                        : (entries[i].type == AKAOS_DT_CHARACTER
                                               ? AKAOS_MODE_CHARACTER | 0600
                                               : 0644);
                    fallback.size = 0;
                    print_long_entry(&fallback, name);
                }
            } else {
                write_str(STDOUT_FILENO, name);
                write_str(STDOUT_FILENO, "\n");
            }
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    int opt_a = 0;
    int opt_l = 0;
    const char *targets[32];
    int target_count = 0;

    int options_done = 0;
    for (int i = 1; i < argc; ++i) {
        if (!options_done && strcmp(argv[i], "--help") == 0) {
            write_str(STDOUT_FILENO, "Usage: ls [-al] [FILE]...\nList directory contents.\n");
            return 0;
        }
        if (!options_done && strcmp(argv[i], "--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done && argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; argv[i][j]; ++j) {
                if (argv[i][j] == 'a') opt_a = 1;
                else if (argv[i][j] == 'l') opt_l = 1;
                else {
                    write_str(STDERR_FILENO, "ls: invalid option -- '");
                    char option[2] = {argv[i][j], '\0'};
                    write_str(STDERR_FILENO, option);
                    write_str(STDERR_FILENO, "'\nTry 'ls --help' for more information.\n");
                    return 1;
                }
            }
        } else {
            if (target_count < 32) {
                targets[target_count++] = argv[i];
            }
        }
    }

    if (target_count == 0) {
        targets[0] = ".";
        target_count = 1;
    }

    int had_error = 0;
    for (int i = 0; i < target_count; ++i) {
        int print_header = (target_count > 1);
        if (i > 0 && print_header) {
            write_str(STDOUT_FILENO, "\n");
        }
        if (list_path(targets[i], opt_a, opt_l, print_header) != 0) {
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
