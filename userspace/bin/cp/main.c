#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#define VERSION "8.32 (akaOS coreutils)"

/* Configuration and option flags */
static int opt_archive = 0;       /* -a: same as -dpR */
static int opt_backup = 0;        /* -b, --backup */
static int opt_no_deref = 0;      /* -d: preserve links, no dereference */
static int opt_force = 0;         /* -f, --force */
static int opt_interactive = 0;   /* -i, --interactive */
static int opt_link = 0;          /* -l, --link */
static int opt_preserve = 0;      /* -p, --preserve */
static int opt_parents = 0;       /* -P, --parents */
static int opt_recursive = 0;     /* -r, -R, --recursive */
static int opt_symlink = 0;       /* -s, --symbolic-link */
static const char *opt_suffix = "~"; /* -S, --suffix */
static int opt_update = 0;        /* -u, --update */
static int opt_verbose = 0;       /* -v, --verbose */
static const char *opt_sparse = "auto"; /* --sparse=WHEN: auto, always, never */
static const char *opt_version_control = NULL; /* --version-control=WORD */

static char s_io_buf[4096];

static void print_version(void) {
    printf("cp %s\n", VERSION);
}

static void print_usage(void) {
    printf("Usage: cp [OPTION]... SOURCE... DEST\n"
           "  or:  cp [OPTION]... SOURCE... DIRECTORY\n"
           "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n\n"
           "Mandatory arguments to long options are mandatory for short options too.\n"
           "  -a, --archive                same as -dpR\n"
           "  -b, --backup[=CONTROL]       make a backup of each existing destination file\n"
           "  -d, --no-dereference         preserve links\n"
           "  -f, --force                  if an existing destination file cannot be\n"
           "                                 opened, remove it and try again\n"
           "  -i, --interactive            prompt before overwrite (overrides a previous -n option)\n"
           "  -l, --link                   hard link files instead of copying\n"
           "  -p, --preserve               preserve file attributes (mode, ownership, timestamps)\n"
           "  -P, --parents                append source path to DIRECTORY\n"
           "  -r, -R, --recursive          copy directories recursively\n"
           "      --sparse=WHEN            control creation of SPARSE files (auto, always, never)\n"
           "  -s, --symbolic-link          make symbolic links instead of copying\n"
           "  -S, --suffix=SUFFIX          override the usual backup suffix\n"
           "  -u, --update                 copy only when the SOURCE file is newer\n"
           "                                 than the destination file or when the\n"
           "                                 destination file is missing\n"
           "  -v, --verbose                explain what is being done\n"
           "  -V, --version-control=WORD   override the usual version control\n"
           "  -x, --one-file-system        stay on this file system\n"
           "      --help                   display this help and exit\n"
           "      --version                output version information and exit\n\n"
           "The backup suffix is '~', unless set with --suffix or SIMPLE_BACKUP_SUFFIX.\n"
           "The version control method may be selected via the --backup option or through\n"
           "the VERSION_CONTROL environment variable. Here are the values:\n"
           "  none, off       never make backups (even if --backup is given)\n"
           "  numbered, t     make numbered backups (file.~1~, file.~2~, ...)\n"
           "  existing, nil   numbered if numbered backups exist, simple otherwise\n"
           "  simple, never   always make simple backups (file~)\n");
}

static void get_basename_buf(const char *path, char *out, size_t out_size) {
    if (!path || !*path || out_size == 0) {
        if (out_size) out[0] = '\0';
        return;
    }
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') --len;
    int last_slash = -1;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/') last_slash = (int)i;
    }
    const char *start = (last_slash >= 0) ? path + last_slash + 1 : path;
    size_t name_len = (path + len) - start;
    if (name_len >= out_size) name_len = out_size - 1;
    memcpy(out, start, name_len);
    out[name_len] = '\0';
}

static void build_dest_path(const char *dest_dir, const char *src_path, char *out, size_t out_size) {
    char bname[128];
    get_basename_buf(src_path, bname, sizeof(bname));
    if (strcmp(dest_dir, "/") == 0) {
        snprintf(out, out_size, "/%s", bname);
    } else if (strcmp(dest_dir, ".") == 0) {
        snprintf(out, out_size, "%s", bname);
    } else {
        snprintf(out, out_size, "%s/%s", dest_dir, bname);
    }
}

/* Ensure directory hierarchy exists for -P/--parents */
static int ensure_parents_exist(const char *path) {
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
                fprintf(stderr, "cp: cannot make directory '%s': %s\n", cur, strerror(errno));
                return -1;
            }
        }
        token = strtok(NULL, "/");
    }
    return 0;
}

/* Check if numbered backup exists for destination */
static int has_numbered_backups(const char *dst) {
    char test_name[512];
    snprintf(test_name, sizeof(test_name), "%s.~1~", dst);
    struct stat st;
    return (stat(test_name, &st) == 0);
}

/* Generate next numbered backup file name */
static void get_next_numbered_backup(const char *dst, char *out, size_t out_size) {
    int num = 1;
    char candidate[512];
    struct stat st;
    for (;;) {
        snprintf(candidate, sizeof(candidate), "%s.~%d~", dst, num);
        if (stat(candidate, &st) != 0) {
            strncpy(out, candidate, out_size - 1);
            out[out_size - 1] = '\0';
            return;
        }
        num++;
    }
}

/* Perform backup of existing destination file if requested */
static int backup_destination(const char *dst) {
    struct stat st;
    if (stat(dst, &st) != 0) return 0; /* Nothing to back up */

    const char *vc = opt_version_control ? opt_version_control : getenv("VERSION_CONTROL");
    if (!vc) vc = "simple";

    if (strcmp(vc, "none") == 0 || strcmp(vc, "off") == 0) {
        return 0;
    }

    char backup_path[512];
    if (strcmp(vc, "numbered") == 0 || strcmp(vc, "t") == 0) {
        get_next_numbered_backup(dst, backup_path, sizeof(backup_path));
    } else if (strcmp(vc, "existing") == 0 || strcmp(vc, "nil") == 0) {
        if (has_numbered_backups(dst)) {
            get_next_numbered_backup(dst, backup_path, sizeof(backup_path));
        } else {
            snprintf(backup_path, sizeof(backup_path), "%s%s", dst, opt_suffix);
        }
    } else {
        /* default: simple */
        snprintf(backup_path, sizeof(backup_path), "%s%s", dst, opt_suffix);
    }

    if (rename(dst, backup_path) != 0) {
        /* If cross-device or rename fails, try copying then unlinking */
        int sfd = open(dst, O_RDONLY);
        if (sfd < 0) return -1;
        int dfd = open(backup_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (dfd < 0) { close(sfd); return -1; }
        ssize_t n;
        char buf[4096];
        while ((n = read(sfd, buf, sizeof(buf))) > 0) {
            write(dfd, buf, (size_t)n);
        }
        close(sfd);
        close(dfd);
        unlink(dst);
    }
    if (opt_verbose) {
        printf("backed up '%s' to '%s'\n", dst, backup_path);
    }
    return 0;
}

/* Prompt for interactive confirmation */
static int prompt_interactive(const char *dst) {
    struct stat st;
    if (stat(dst, &st) != 0) return 1; /* doesn't exist, proceed */

    if (opt_force) return 1; /* force overrides interactive */

    fprintf(stderr, "cp: overwrite '%s'? ", dst);
    fflush(stderr);

    char reply[64];
    if (!fgets(reply, sizeof(reply), stdin)) return 0;
    if (reply[0] == 'y' || reply[0] == 'Y') return 1;
    return 0;
}

static int preserve_metadata(int fd, const char *dst, const struct stat *source) {
    struct stat current;
    /* Avoid a privileged ownership operation when ownership already matches. */
    if (fstat(fd, &current) != 0 ||
        ((current.st_uid != source->st_uid || current.st_gid != source->st_gid) &&
         fchown(fd, source->st_uid, source->st_gid) != 0) ||
        fchmod(fd, source->st_mode & 07777) != 0) {
        fprintf(stderr, "cp: cannot preserve attributes of '%s': %s\n", dst, strerror(errno));
        return 1;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst);

static int copy_dir(const char *src, const char *dst) {
    size_t slen = strlen(src);
    size_t dlen = strlen(dst);
    if (dlen >= slen && strncmp(src, dst, slen) == 0 && (dst[slen] == '/' || dst[slen] == '\0')) {
        fprintf(stderr, "cp: cannot copy a directory, '%s', into itself, '%s'\n", src, dst);
        return 1;
    }

    struct stat dst_st;
    if (stat(dst, &dst_st) != 0) {
        if (mkdir(dst, 0755) != 0) {
            fprintf(stderr, "cp: cannot create directory '%s': %s\n", dst, strerror(errno));
            return 1;
        }
        if (opt_verbose) {
            printf("'%s' -> '%s'\n", src, dst);
        }
    } else if (!S_ISDIR(dst_st.st_mode)) {
        fprintf(stderr, "cp: cannot overwrite non-directory '%s' with directory '%s'\n", dst, src);
        return 1;
    }

    int dst_fd = -1;
    if (opt_preserve) {
        dst_fd = open(dst, O_RDONLY | O_DIRECTORY);
        if (dst_fd < 0) {
            fprintf(stderr, "cp: cannot open directory '%s': %s\n", dst, strerror(errno));
            return 1;
        }
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "cp: cannot open directory '%s': %s\n", src, strerror(errno));
        if (dst_fd >= 0) close(dst_fd);
        return 1;
    }

    struct akaos_dirent *entries = malloc(sizeof(struct akaos_dirent) * 32);
    if (!entries) {
        fprintf(stderr, "cp: out of memory\n");
        close(src_fd);
        if (dst_fd >= 0) close(dst_fd);
        return 1;
    }

    int had_error = 0;
    for (;;) {
        ssize_t count = getdents(src_fd, entries, 32);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (!name || !*name) continue;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

            char child_src[512];
            char child_dst[512];

            if (strcmp(src, "/") == 0) {
                snprintf(child_src, sizeof(child_src), "/%s", name);
            } else {
                snprintf(child_src, sizeof(child_src), "%s/%s", src, name);
            }

            if (strcmp(dst, "/") == 0) {
                snprintf(child_dst, sizeof(child_dst), "/%s", name);
            } else {
                snprintf(child_dst, sizeof(child_dst), "%s/%s", dst, name);
            }

            if (copy_file(child_src, child_dst) != 0) {
                had_error = 1;
            }
        }
    }

    free(entries);
    close(src_fd);

    if (opt_preserve) {
        struct stat src_st;
        if (stat(src, &src_st) == 0) {
            had_error |= preserve_metadata(dst_fd, dst, &src_st);
        }
    }

    if (dst_fd >= 0) close(dst_fd);
    return had_error;
}

static int copy_file(const char *src, const char *dst) {
    struct stat src_st;
    if (stat(src, &src_st) != 0) {
        fprintf(stderr, "cp: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(src_st.st_mode)) {
        if (!opt_recursive) {
            fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
            return 1;
        }
        return copy_dir(src, dst);
    }

    struct stat dst_st;
    int dst_exists = (stat(dst, &dst_st) == 0);

    /* Special case: same file */
    if (dst_exists && src_st.st_ino == dst_st.st_ino) {
        if (opt_force && opt_backup) {
            backup_destination(dst);
        } else {
            fprintf(stderr, "cp: '%s' and '%s' are the same file\n", src, dst);
            return 1;
        }
    }

    /* -u, --update: copy only if src is newer than dst or dst is missing */
    if (opt_update && dst_exists) {
        if (src_st.st_mtime <= dst_st.st_mtime) {
            return 0; /* Skip up-to-date file */
        }
    }

    /* -i, --interactive: confirmation prompt */
    if (opt_interactive && dst_exists) {
        if (!prompt_interactive(dst)) {
            return 0;
        }
    }

    /* -b, --backup: make backup of dst if it exists */
    if (opt_backup && dst_exists) {
        backup_destination(dst);
        dst_exists = (stat(dst, &dst_st) == 0);
    }

    /* -l, --link: hard link instead of copying */
    if (opt_link) {
        if (dst_exists && opt_force) {
            unlink(dst);
        }
        if (link(src, dst) == 0) {
            if (opt_verbose) printf("'%s' -> '%s'\n", src, dst);
            return 0;
        }
        /* Fall back to copy if link fails or is not supported across devices */
    }

    /* -s, --symbolic-link: make symbolic link instead of copying */
    if (opt_symlink) {
        if (dst_exists && opt_force) {
            unlink(dst);
        }
        if (symlink(src, dst) == 0) {
            if (opt_verbose) printf("'%s' -> '%s'\n", src, dst);
            return 0;
        }
        /* If symlink not supported on akaOS VFS, print warning or proceed with copy */
        fprintf(stderr, "cp: cannot create symbolic link '%s': %s\n", dst, strerror(errno));
        return 1;
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "cp: cannot open '%s' for reading: %s\n", src, strerror(errno));
        return 1;
    }

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (dst_fd < 0 && opt_force) {
        unlink(dst);
        dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    }
    if (dst_fd < 0) {
        fprintf(stderr, "cp: cannot create regular file '%s': %s\n", dst, strerror(errno));
        close(src_fd);
        return 1;
    }

    int had_error = 0;
    ssize_t nread;
    int is_sparse_mode = (strcmp(opt_sparse, "always") == 0);

    while ((nread = read(src_fd, s_io_buf, sizeof(s_io_buf))) > 0) {
        /* Sparse detection heuristic */
        if (is_sparse_mode || strcmp(opt_sparse, "auto") == 0) {
            int all_zeros = 1;
            for (ssize_t z = 0; z < nread; ++z) {
                if (s_io_buf[z] != 0) {
                    all_zeros = 0;
                    break;
                }
            }
            if (all_zeros && nread >= 512) {
                /* Sparse hole optimization */
                lseek(dst_fd, nread, SEEK_CUR);
                continue;
            }
        }

        ssize_t written = 0;
        while (written < nread) {
            ssize_t nwritten = write(dst_fd, s_io_buf + written, (size_t)(nread - written));
            if (nwritten <= 0) {
                fprintf(stderr, "cp: error writing to '%s': %s\n", dst, strerror(errno));
                had_error = 1;
                break;
            }
            written += nwritten;
        }
        if (had_error) break;
    }
    if (nread < 0) {
        fprintf(stderr, "cp: error reading '%s': %s\n", src, strerror(errno));
        had_error = 1;
    }

    close(src_fd);

    if (!had_error) {
        if (opt_preserve) {
            had_error = preserve_metadata(dst_fd, dst, &src_st);
            struct utimbuf ut;
            ut.actime = src_st.st_atime;
            ut.modtime = src_st.st_mtime;
            utime(dst, &ut);
        }
        if (opt_verbose) {
            printf("'%s' -> '%s'\n", src, dst);
        }
    }

    close(dst_fd);
    return had_error;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *simple_env = getenv("SIMPLE_BACKUP_SUFFIX");
    if (simple_env && *simple_env) {
        opt_suffix = simple_env;
    }

    int options_done = 0;
    int first_src_idx = -1;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (!options_done && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                options_done = 1;
                continue;
            }
            if (strcmp(arg, "--help") == 0) {
                print_usage();
                return 0;
            }
            if (strcmp(arg, "--version") == 0) {
                print_version();
                return 0;
            }
            if (strcmp(arg, "--archive") == 0) {
                opt_archive = 1;
                opt_recursive = 1;
                opt_preserve = 1;
                opt_no_deref = 1;
                continue;
            }
            if (strcmp(arg, "--backup") == 0) {
                opt_backup = 1;
                continue;
            }
            if (strncmp(arg, "--backup=", 9) == 0) {
                opt_backup = 1;
                opt_version_control = arg + 9;
                continue;
            }
            if (strcmp(arg, "--no-dereference") == 0) {
                opt_no_deref = 1;
                continue;
            }
            if (strcmp(arg, "--force") == 0) {
                opt_force = 1;
                continue;
            }
            if (strcmp(arg, "--interactive") == 0) {
                opt_interactive = 1;
                continue;
            }
            if (strcmp(arg, "--link") == 0) {
                opt_link = 1;
                continue;
            }
            if (strcmp(arg, "--preserve") == 0) {
                opt_preserve = 1;
                continue;
            }
            if (strcmp(arg, "--parents") == 0) {
                opt_parents = 1;
                continue;
            }
            if (strcmp(arg, "--recursive") == 0) {
                opt_recursive = 1;
                continue;
            }
            if (strcmp(arg, "--symbolic-link") == 0) {
                opt_symlink = 1;
                continue;
            }
            if (strncmp(arg, "--suffix=", 9) == 0) {
                opt_suffix = arg + 9;
                continue;
            }
            if (strcmp(arg, "--update") == 0) {
                opt_update = 1;
                continue;
            }
            if (strcmp(arg, "--verbose") == 0) {
                opt_verbose = 1;
                continue;
            }
            if (strncmp(arg, "--version-control=", 18) == 0) {
                opt_backup = 1;
                opt_version_control = arg + 18;
                continue;
            }
            if (strcmp(arg, "--one-file-system") == 0) {
                continue; /* no-op in single VFS mount */
            }
            if (strncmp(arg, "--sparse=", 9) == 0) {
                opt_sparse = arg + 9;
                continue;
            }

            /* Single-letter options parser */
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'a') {
                    opt_archive = 1;
                    opt_recursive = 1;
                    opt_preserve = 1;
                    opt_no_deref = 1;
                } else if (arg[c] == 'b') {
                    opt_backup = 1;
                } else if (arg[c] == 'd') {
                    opt_no_deref = 1;
                } else if (arg[c] == 'f') {
                    opt_force = 1;
                } else if (arg[c] == 'i') {
                    opt_interactive = 1;
                } else if (arg[c] == 'l') {
                    opt_link = 1;
                } else if (arg[c] == 'p') {
                    opt_preserve = 1;
                } else if (arg[c] == 'P') {
                    opt_parents = 1;
                } else if (arg[c] == 'r' || arg[c] == 'R') {
                    opt_recursive = 1;
                } else if (arg[c] == 's') {
                    opt_symlink = 1;
                } else if (arg[c] == 'S') {
                    if (arg[c + 1] != '\0') {
                        opt_suffix = &arg[c + 1];
                        break;
                    } else if (i + 1 < argc) {
                        opt_suffix = argv[++i];
                        break;
                    }
                } else if (arg[c] == 'u') {
                    opt_update = 1;
                } else if (arg[c] == 'v') {
                    opt_verbose = 1;
                } else if (arg[c] == 'V') {
                    opt_backup = 1;
                    if (arg[c + 1] != '\0') {
                        opt_version_control = &arg[c + 1];
                        break;
                    } else if (i + 1 < argc) {
                        opt_version_control = argv[++i];
                        break;
                    }
                } else if (arg[c] == 'x') {
                    /* one-file-system: no-op */
                } else if (arg[c] == 'h') {
                    print_usage();
                    return 0;
                } else {
                    fprintf(stderr, "cp: invalid option -- '%c'\n", arg[c]);
                    print_usage();
                    return 1;
                }
            }
            continue;
        }
        if (first_src_idx < 0) {
            first_src_idx = i;
        }
    }

    if (first_src_idx < 0 || first_src_idx >= argc - 1) {
        fprintf(stderr, "cp: missing destination file operand after '%s'\n",
                first_src_idx >= 0 ? argv[first_src_idx] : "");
        return 1;
    }

    const char *dest = argv[argc - 1];
    int num_sources = (argc - 1) - first_src_idx;

    struct stat dest_st;
    int dest_is_dir = (stat(dest, &dest_st) == 0 && S_ISDIR(dest_st.st_mode));

    if (num_sources > 1 && !dest_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", dest);
        return 1;
    }

    char target_path[512];
    int had_error = 0;

    for (int i = first_src_idx; i < argc - 1; ++i) {
        const char *src = argv[i];
        if (dest_is_dir) {
            if (opt_parents) {
                /* Append full source path to DIRECTORY */
                snprintf(target_path, sizeof(target_path), "%s/%s", dest, src);
                ensure_parents_exist(target_path);
            } else {
                build_dest_path(dest, src, target_path, sizeof(target_path));
            }
        } else {
            snprintf(target_path, sizeof(target_path), "%s", dest);
        }

        if (copy_file(src, target_path) != 0) {
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
