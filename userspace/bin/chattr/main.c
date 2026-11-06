#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHATTR_VERSION "1.47.2"
#define CHATTR_DATE "1-Jan-2025"

/* Linux/ext4 file attributes ioctl definitions */
#ifndef FS_IOC_GETFLAGS
#define FS_IOC_GETFLAGS 0x80086601UL
#endif
#ifndef FS_IOC_SETFLAGS
#define FS_IOC_SETFLAGS 0x40086602UL
#endif
#ifndef FS_IOC_GETVERSION
#define FS_IOC_GETVERSION 0x80087601UL
#endif
#ifndef FS_IOC_SETVERSION
#define FS_IOC_SETVERSION 0x40087602UL
#endif

/* Standard flag bitmasks */
#define FS_SECRM_FL        0x00000001 /* 's' Secure deletion */
#define FS_UNRM_FL         0x00000002 /* 'u' Undelete */
#define FS_COMPR_FL        0x00000004 /* 'c' Compress file */
#define FS_SYNC_FL         0x00000008 /* 'S' Synchronous updates */
#define FS_IMMUTABLE_FL    0x00000010 /* 'i' Immutable file */
#define FS_APPEND_FL       0x00000020 /* 'a' Append only */
#define FS_NODUMP_FL       0x00000040 /* 'd' Do not dump */
#define FS_NOATIME_FL      0x00000080 /* 'A' Do not update atime */
#define FS_DIRSYNC_FL      0x00010000 /* 'D' Synchronous directory updates */
#define FS_JOURNAL_DATA_FL 0x00004000 /* 'j' Data journalling */
#define FS_NOTAIL_FL       0x00008000 /* 't' No tail-merging */
#define FS_TOPDIR_FL       0x00020000 /* 'T' Top of directory hierarchy */
#define FS_EXTENTS_FL      0x00080000 /* 'e' Extents format */

static int opt_recursive = 0;
static int opt_verbose = 0;
static int opt_suppress = 0;
static int has_version = 0;
static unsigned long new_version = 0;

enum op_type {
    OP_NONE = 0,
    OP_ADD,
    OP_REMOVE,
    OP_SET
};

static enum op_type current_op = OP_NONE;
static unsigned long add_mask = 0;
static unsigned long rem_mask = 0;
static unsigned long set_mask = 0;

static void print_help(void) {
    printf("Usage: chattr [-RVf] [-+=aAcCdDeijPsStTuFx] [-p project] [-v version] files...\n");
}

static unsigned long char_to_flag(char c) {
    switch (c) {
        case 'a': return FS_APPEND_FL;
        case 'A': return FS_NOATIME_FL;
        case 'c': return FS_COMPR_FL;
        case 'd': return FS_NODUMP_FL;
        case 'D': return FS_DIRSYNC_FL;
        case 'e': return FS_EXTENTS_FL;
        case 'i': return FS_IMMUTABLE_FL;
        case 'j': return FS_JOURNAL_DATA_FL;
        case 's': return FS_SECRM_FL;
        case 'S': return FS_SYNC_FL;
        case 't': return FS_NOTAIL_FL;
        case 'T': return FS_TOPDIR_FL;
        case 'u': return FS_UNRM_FL;
        default:  return 0;
    }
}

static void flags_to_string(unsigned long f, char buf[23]) {
    /* 22 positions per e2fsprogs lsattr/chattr standard output:
     * 0:s, 1:u, 2:S, 3:D, 4:i, 5:a, 6:d, 7:A, 8:c, 9:-, 10:j, 11:-, 12:t, 13:T, 14:e, 15..21:-
     */
    for (int i = 0; i < 22; ++i) buf[i] = '-';
    buf[22] = '\0';

    if (f & FS_SECRM_FL)        buf[0] = 's';
    if (f & FS_UNRM_FL)         buf[1] = 'u';
    if (f & FS_SYNC_FL)         buf[2] = 'S';
    if (f & FS_DIRSYNC_FL)      buf[3] = 'D';
    if (f & FS_IMMUTABLE_FL)    buf[4] = 'i';
    if (f & FS_APPEND_FL)       buf[5] = 'a';
    if (f & FS_NODUMP_FL)       buf[6] = 'd';
    if (f & FS_NOATIME_FL)      buf[7] = 'A';
    if (f & FS_COMPR_FL)        buf[8] = 'c';
    if (f & FS_JOURNAL_DATA_FL) buf[10] = 'j';
    if (f & FS_NOTAIL_FL)       buf[12] = 't';
    if (f & FS_TOPDIR_FL)       buf[13] = 'T';
    if (f & FS_EXTENTS_FL)      buf[14] = 'e';
}

static int parse_mode(const char *mode) {
    if (!mode || !*mode) return 0;
    char op = mode[0];
    if (op != '+' && op != '-' && op != '=') return 0;

    current_op = (op == '+') ? OP_ADD : ((op == '-') ? OP_REMOVE : OP_SET);

    unsigned long mask = 0;
    for (size_t i = 1; mode[i]; ++i) {
        unsigned long flag = char_to_flag(mode[i]);
        if (!flag) {
            fprintf(stderr, "chattr: %s: Unknown flag '%c'\n", mode, mode[i]);
            return 0;
        }
        mask |= flag;
    }

    if (current_op == OP_ADD) add_mask = mask;
    else if (current_op == OP_REMOVE) rem_mask = mask;
    else set_mask = mask;

    return 1;
}

static int process_file(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (!opt_suppress) {
            fprintf(stderr, "chattr: No such file or directory while trying to stat %s\n", path);
        }
        return 1;
    }

    int fd = open(path, O_RDONLY | O_NONBLOCK);
    unsigned long flags = 0;
    int got_flags = 0;
    if (fd >= 0) {
        if (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0) {
            got_flags = 1;
        }
    }

    unsigned long new_flags = flags;
    if (current_op == OP_ADD) {
        new_flags |= add_mask;
    } else if (current_op == OP_REMOVE) {
        new_flags &= ~rem_mask;
    } else if (current_op == OP_SET) {
        new_flags = set_mask;
    }

    if (opt_verbose) {
        char flag_str[23];
        flags_to_string(new_flags, flag_str);
        printf("Flags of %s set as %s\n", path, flag_str);
        if (has_version) {
            printf("Version of %s set as %lu\n", path, new_version);
        }
    }

    int had_err = 0;
    if (fd >= 0) {
        if (got_flags && current_op != OP_NONE) {
            if (ioctl(fd, FS_IOC_SETFLAGS, &new_flags) != 0 && errno != ENOTTY && errno != EINVAL) {
                if (!opt_suppress) {
                    fprintf(stderr, "chattr: %s: %s\n", path, strerror(errno));
                }
                had_err = 1;
            }
        }
        if (has_version) {
            if (ioctl(fd, FS_IOC_SETVERSION, &new_version) != 0 && errno != ENOTTY && errno != EINVAL) {
                if (!opt_suppress) {
                    fprintf(stderr, "chattr: %s: %s\n", path, strerror(errno));
                }
                had_err = 1;
            }
        }
        close(fd);
    }

    if (S_ISDIR(st.st_mode) && opt_recursive) {
        DIR *dir = opendir(path);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                char subpath[1024];
                snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
                if (process_file(subpath) != 0) had_err = 1;
            }
            closedir(dir);
        }
    }

    return had_err;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    int files_start = argc;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("chattr " CHATTR_VERSION " (" CHATTR_DATE ")\n");
            return 0;
        }
        if (arg[0] == '-' && (arg[1] == 'R' || arg[1] == 'V' || arg[1] == 'f' || arg[1] == 'v')) {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'R') {
                    opt_recursive = 1;
                } else if (arg[c] == 'V') {
                    opt_verbose = 1;
                } else if (arg[c] == 'f') {
                    opt_suppress = 1;
                } else if (arg[c] == 'v') {
                    has_version = 1;
                    if (arg[c + 1] != '\0') {
                        new_version = strtoul(arg + c + 1, NULL, 10);
                        break;
                    } else if (i + 1 < argc) {
                        new_version = strtoul(argv[++i], NULL, 10);
                        break;
                    }
                } else {
                    /* Could be mode flag e.g. -i */
                    if (parse_mode(arg)) {
                        break;
                    }
                }
            }
            continue;
        }
        if (arg[0] == '+' || arg[0] == '-' || arg[0] == '=') {
            if (parse_mode(arg)) {
                continue;
            }
        }
        files_start = i;
        break;
    }

    if (opt_verbose) {
        fprintf(stderr, "chattr " CHATTR_VERSION " (" CHATTR_DATE ")\n");
    }

    if (files_start >= argc) {
        if (!has_version && current_op == OP_NONE) {
            fprintf(stderr, "Must use '-v', =, - or +\n");
        } else {
            fprintf(stderr, "chattr: No files specified\n");
        }
        return 1;
    }

    int had_err = 0;
    for (int i = files_start; i < argc; ++i) {
        if (process_file(argv[i]) != 0) had_err = 1;
    }

    return had_err ? 1 : 0;
}
