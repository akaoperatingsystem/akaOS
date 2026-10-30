#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_io_buf[4096];

static void print_usage(void) {
    printf("Usage: cp [OPTION]... SOURCE... DEST\n");
    printf("Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n\n");
    printf("Options:\n");
    printf("  -r, -R       copy directories recursively\n");
    printf("  -f           force copy by removing existing destination files\n");
    printf("  -v           explain what is being done\n");
    printf("  -p           preserve file mode and ownership\n");
    printf("  -h, --help   display this help and exit\n");
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
    char bname[64];
    get_basename_buf(src_path, bname, sizeof(bname));
    if (strcmp(dest_dir, "/") == 0) {
        snprintf(out, out_size, "/%s", bname);
    } else if (strcmp(dest_dir, ".") == 0) {
        snprintf(out, out_size, "%s", bname);
    } else {
        snprintf(out, out_size, "%s/%s", dest_dir, bname);
    }
}

static int copy_file(const char *src, const char *dst, int opt_r, int opt_f, int opt_v, int opt_p);

static int copy_dir(const char *src, const char *dst, int opt_f, int opt_v, int opt_p) {
    size_t slen = strlen(src);
    size_t dlen = strlen(dst);
    if (dlen >= slen && strncmp(src, dst, slen) == 0 && (dst[slen] == '/' || dst[slen] == '\0')) {
        fprintf(stderr, "cp: cannot copy a directory, '%s', into itself, '%s'\n", src, dst);
        return 1;
    }

    struct akaos_stat dst_st;
    if (stat(dst, &dst_st) != 0) {
        if (mkdir(dst, 0755) != 0) {
            fprintf(stderr, "cp: cannot create directory '%s': %s\n", dst, strerror(errno));
            return 1;
        }
        if (opt_v) {
            printf("'%s' -> '%s'\n", src, dst);
        }
    } else if (!S_ISDIR(dst_st.mode)) {
        fprintf(stderr, "cp: cannot overwrite non-directory '%s' with directory '%s'\n", dst, src);
        return 1;
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "cp: cannot open directory '%s': %s\n", src, strerror(errno));
        return 1;
    }

    struct akaos_dirent *entries = malloc(sizeof(struct akaos_dirent) * 16);
    if (!entries) {
        fprintf(stderr, "cp: out of memory\n");
        close(src_fd);
        return 1;
    }

    int had_error = 0;
    for (;;) {
        ssize_t count = getdents(src_fd, entries, 16);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (!name || !*name) continue;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

            char *child_src = malloc(256);
            char *child_dst = malloc(256);
            if (!child_src || !child_dst) {
                fprintf(stderr, "cp: out of memory\n");
                if (child_src) free(child_src);
                if (child_dst) free(child_dst);
                had_error = 1;
                break;
            }

            if (strcmp(src, "/") == 0) {
                snprintf(child_src, 256, "/%s", name);
            } else {
                snprintf(child_src, 256, "%s/%s", src, name);
            }

            if (strcmp(dst, "/") == 0) {
                snprintf(child_dst, 256, "/%s", name);
            } else {
                snprintf(child_dst, 256, "%s/%s", dst, name);
            }

            if (copy_file(child_src, child_dst, 1, opt_f, opt_v, opt_p) != 0) {
                had_error = 1;
            }

            free(child_src);
            free(child_dst);
        }
    }

    free(entries);
    close(src_fd);

    if (opt_p) {
        struct akaos_stat src_st;
        if (stat(src, &src_st) == 0) {
            chmod(dst, src_st.mode & 07777);
            chown(dst, src_st.uid, src_st.gid);
        }
    }

    return had_error;
}

static int copy_file(const char *src, const char *dst, int opt_r, int opt_f, int opt_v, int opt_p) {
    struct akaos_stat src_st;
    if (stat(src, &src_st) != 0) {
        fprintf(stderr, "cp: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(src_st.mode)) {
        if (!opt_r) {
            fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
            return 1;
        }
        return copy_dir(src, dst, opt_f, opt_v, opt_p);
    }

    struct akaos_stat dst_st;
    if (stat(dst, &dst_st) == 0 && src_st.inode == dst_st.inode) {
        fprintf(stderr, "cp: '%s' and '%s' are the same file\n", src, dst);
        return 1;
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "cp: cannot open '%s' for reading: %s\n", src, strerror(errno));
        return 1;
    }

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (dst_fd < 0 && opt_f) {
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
    while ((nread = read(src_fd, s_io_buf, sizeof(s_io_buf))) > 0) {
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
    close(dst_fd);

    if (!had_error) {
        if (opt_p) {
            chmod(dst, src_st.mode & 07777);
            chown(dst, src_st.uid, src_st.gid);
        }
        if (opt_v) {
            printf("'%s' -> '%s'\n", src, dst);
        }
    }

    return had_error;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int opt_r = 0;
    int opt_f = 0;
    int opt_v = 0;
    int opt_p = 0;
    int options_done = 0;
    int first_src_idx = -1;

    for (int i = 1; i < argc; ++i) {
        if (!options_done && argv[i][0] == '-' && argv[i][1] != '\0') {
            if (strcmp(argv[i], "--") == 0) {
                options_done = 1;
                continue;
            }
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_usage();
                return 0;
            }
            for (size_t c = 1; argv[i][c]; ++c) {
                if (argv[i][c] == 'r' || argv[i][c] == 'R') {
                    opt_r = 1;
                } else if (argv[i][c] == 'f') {
                    opt_f = 1;
                } else if (argv[i][c] == 'v') {
                    opt_v = 1;
                } else if (argv[i][c] == 'p') {
                    opt_p = 1;
                } else {
                    fprintf(stderr, "cp: invalid option -- '%c'\n", argv[i][c]);
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

    struct akaos_stat dest_st;
    int dest_is_dir = (stat(dest, &dest_st) == 0 && S_ISDIR(dest_st.mode));

    if (num_sources > 1 && !dest_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", dest);
        return 1;
    }

    char *target_path = malloc(256);
    if (!target_path) {
        fprintf(stderr, "cp: out of memory\n");
        return 1;
    }

    int had_error = 0;
    for (int i = first_src_idx; i < argc - 1; ++i) {
        const char *src = argv[i];
        if (dest_is_dir) {
            build_dest_path(dest, src, target_path, 256);
        } else {
            snprintf(target_path, 256, "%s", dest);
        }

        if (copy_file(src, target_path, opt_r, opt_f, opt_v, opt_p) != 0) {
            had_error = 1;
        }
    }

    free(target_path);
    return had_error ? 1 : 0;
}
