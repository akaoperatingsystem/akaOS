#include <akaos/fs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_usage(void) {
    printf("Usage: mv [OPTION]... SOURCE... DEST\n");
    printf("Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n\n");
    printf("Options:\n");
    printf("  -f           do not prompt before overwriting\n");
    printf("  -v           explain what is being done\n");
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

static int move_entry(const char *src, const char *dst, int opt_f, int opt_v) {
    struct akaos_stat src_st;
    if (stat(src, &src_st) != 0) {
        fprintf(stderr, "mv: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    struct akaos_stat dst_st;
    if (stat(dst, &dst_st) == 0 && src_st.inode == dst_st.inode) {
        fprintf(stderr, "mv: '%s' and '%s' are the same file\n", src, dst);
        return 1;
    }

    int res = rename(src, dst);
    if (res != 0 && opt_f) {
        if (stat(dst, &dst_st) == 0 && !S_ISDIR(dst_st.mode)) {
            unlink(dst);
            res = rename(src, dst);
        }
    }

    if (res != 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
        return 1;
    }

    if (opt_v) {
        printf("renamed '%s' -> '%s'\n", src, dst);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int opt_f = 0;
    int opt_v = 0;
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
                if (argv[i][c] == 'f') {
                    opt_f = 1;
                } else if (argv[i][c] == 'v') {
                    opt_v = 1;
                } else {
                    fprintf(stderr, "mv: invalid option -- '%c'\n", argv[i][c]);
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
        fprintf(stderr, "mv: missing destination file operand after '%s'\n",
                first_src_idx >= 0 ? argv[first_src_idx] : "");
        return 1;
    }

    const char *dest = argv[argc - 1];
    int num_sources = (argc - 1) - first_src_idx;

    struct akaos_stat dest_st;
    int dest_is_dir = (stat(dest, &dest_st) == 0 && S_ISDIR(dest_st.mode));

    if (num_sources > 1 && !dest_is_dir) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", dest);
        return 1;
    }

    char *target_path = malloc(256);
    if (!target_path) {
        fprintf(stderr, "mv: out of memory\n");
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

        if (move_entry(src, target_path, opt_f, opt_v) != 0) {
            had_error = 1;
        }
    }

    free(target_path);
    return had_error ? 1 : 0;
}
