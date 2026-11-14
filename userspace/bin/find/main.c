#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define FIND_VERSION "4.9.0 (akaOS coreutils)"

struct find_criteria {
    const char *name_pattern;
    const char *iname_pattern;
    char type_filter;       /* 'f', 'd', 'c', 'b', 'l', 'p', 's', or 0 */
    int max_depth;          /* -1 for unlimited */
    int min_depth;          /* 0 by default */
    int opt_empty;          /* -empty */
    int opt_print0;         /* -print0 */
    int opt_delete;         /* -delete */
    char **exec_argv;
    int exec_argc;
};

static int case_insensitive_glob_match(const char *pat, const char *str) {
    while (*pat && *str) {
        if (*pat == '*') {
            while (*(pat + 1) == '*') pat++;
            if (!*(pat + 1)) return 1;
            while (*str) {
                if (case_insensitive_glob_match(pat + 1, str)) return 1;
                str++;
            }
            return 0;
        } else if (*pat == '?') {
            pat++;
            str++;
        } else {
            char c1 = *pat;
            char c2 = *str;
            if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
            if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
            if (c1 != c2) return 0;
            pat++;
            str++;
        }
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

static int glob_match(const char *pat, const char *str) {
    while (*pat && *str) {
        if (*pat == '*') {
            while (*(pat + 1) == '*') pat++;
            if (!*(pat + 1)) return 1;
            while (*str) {
                if (glob_match(pat + 1, str)) return 1;
                str++;
            }
            return 0;
        } else if (*pat == '?' || *pat == *str) {
            pat++;
            str++;
        } else {
            return 0;
        }
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

static const char *get_basename(const char *path) {
    const char *last = path;
    while (*path) {
        if (*path == '/' && *(path + 1) && *(path + 1) != '/') {
            last = path + 1;
        }
        path++;
    }
    return last;
}

static int is_dir_empty(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct akaos_dirent entries[16];
    int empty = 1;
    for (;;) {
        ssize_t count = getdents(fd, entries, 16);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (!name || !*name) continue;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            empty = 0;
            break;
        }
        if (!empty) break;
    }
    close(fd);
    return empty;
}

static int matches_criteria(const char *path, const struct akaos_stat *st,
                            const struct find_criteria *crit, int current_depth) {
    if (crit->min_depth >= 0 && current_depth < crit->min_depth) return 0;
    if (crit->max_depth >= 0 && current_depth > crit->max_depth) return 0;

    if (crit->type_filter) {
        if (crit->type_filter == 'f' && !S_ISREG(st->mode)) return 0;
        if (crit->type_filter == 'd' && !S_ISDIR(st->mode)) return 0;
        if (crit->type_filter == 'c' && !S_ISCHR(st->mode)) return 0;
        if (crit->type_filter == 'b' && !S_ISBLK(st->mode)) return 0;
        if (crit->type_filter == 'l' && !S_ISLNK(st->mode)) return 0;
        if (crit->type_filter == 'p' && !S_ISFIFO(st->mode)) return 0;
        if (crit->type_filter == 's' && !S_ISSOCK(st->mode)) return 0;
    }
    if (crit->name_pattern) {
        const char *bname = get_basename(path);
        if (!glob_match(crit->name_pattern, bname)) return 0;
    }
    if (crit->iname_pattern) {
        const char *bname = get_basename(path);
        if (!case_insensitive_glob_match(crit->iname_pattern, bname)) return 0;
    }
    if (crit->opt_empty) {
        if (S_ISREG(st->mode)) {
            if (st->size != 0) return 0;
        } else if (S_ISDIR(st->mode)) {
            if (!is_dir_empty(path)) return 0;
        } else {
            return 0;
        }
    }
    return 1;
}

static void execute_on_file(const char *path, const struct find_criteria *crit) {
    if (crit->opt_delete) {
        /* If directory, rmdir; if file, unlink */
        struct akaos_stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.mode)) {
                rmdir(path);
            } else {
                unlink(path);
            }
        }
        return;
    }

    if (crit->exec_argc > 0) {
        char *args[32];
        int ac = 0;
        for (int i = 0; i < crit->exec_argc && ac < 31; ++i) {
            if (strcmp(crit->exec_argv[i], "{}") == 0) {
                args[ac++] = (char *)path;
            } else {
                args[ac++] = crit->exec_argv[i];
            }
        }
        args[ac] = NULL;
        pid_t pid = fork();
        if (pid == 0) {
            execv(args[0], args);
            exit(1);
        } else if (pid > 0) {
            waitpid(pid, NULL, 0);
        }
    } else {
        if (crit->opt_print0) {
            printf("%s%c", path, '\0');
        } else {
            printf("%s\n", path);
        }
    }
}

static void walk_dir(const char *path, int current_depth,
                     const struct find_criteria *crit) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) return;

    if (matches_criteria(path, &st, crit, current_depth)) {
        execute_on_file(path, crit);
    }

    if (!S_ISDIR(st.mode)) return;
    if (crit->max_depth >= 0 && current_depth >= crit->max_depth) return;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    struct akaos_dirent *entries = malloc(sizeof(struct akaos_dirent) * 16);
    if (!entries) {
        close(fd);
        return;
    }

    for (;;) {
        ssize_t count = getdents(fd, entries, 16);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *name = entries[i].name;
            if (!name || !*name) continue;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

            size_t plen = strlen(path);
            size_t nlen = strlen(name);
            char *child = malloc(plen + nlen + 2);
            if (!child) continue;

            if (strcmp(path, "/") == 0) {
                snprintf(child, plen + nlen + 2, "/%s", name);
            } else {
                snprintf(child, plen + nlen + 2, "%s/%s", path, name);
            }

            walk_dir(child, current_depth + 1, crit);
            free(child);
        }
    }

    free(entries);
    close(fd);
}

static void print_usage(void) {
    printf("Usage: find [-H] [-L] [-P] [PATH...] [EXPRESSION...]\n"
           "Search for files in a directory hierarchy.\n\n"
           "Expressions:\n"
           "  -name PATTERN     Match filename using pattern\n"
           "  -iname PATTERN    Case-insensitive match filename using pattern\n"
           "  -type [f|d|c|b|l] Filter by file type\n"
           "  -maxdepth N       Descend at most N directory levels\n"
           "  -mindepth N       Do not apply tests at levels less than N\n"
           "  -empty            Match empty files or directories\n"
           "  -delete           Delete matched files or empty directories\n"
           "  -print            Print matching path (default)\n"
           "  -print0           Print full path followed by null character\n"
           "  -exec CMD... {} ; Execute CMD with {} replaced by path\n"
           "  -v, --version     Display version information and exit\n"
           "  -h, --help        Display this help and exit\n");
}

int main(int argc, char **argv) {
    const char *paths[16];
    int path_count = 0;
    struct find_criteria crit;
    crit.name_pattern = NULL;
    crit.iname_pattern = NULL;
    crit.type_filter = 0;
    crit.max_depth = -1;
    crit.min_depth = 0;
    crit.opt_empty = 0;
    crit.opt_print0 = 0;
    crit.opt_delete = 0;
    crit.exec_argv = NULL;
    crit.exec_argc = 0;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("find %s\n", FIND_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "-P") == 0) {
            /* Symlink modes acknowledged */
            i++;
            continue;
        }
        if (argv[i][0] != '-') {
            if (path_count < 16) {
                paths[path_count++] = argv[i];
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            crit.name_pattern = argv[++i];
        } else if (strcmp(argv[i], "-iname") == 0 && i + 1 < argc) {
            crit.iname_pattern = argv[++i];
        } else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
            crit.type_filter = argv[++i][0];
        } else if (strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc) {
            crit.max_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-mindepth") == 0 && i + 1 < argc) {
            crit.min_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-empty") == 0) {
            crit.opt_empty = 1;
        } else if (strcmp(argv[i], "-delete") == 0) {
            crit.opt_delete = 1;
        } else if (strcmp(argv[i], "-print0") == 0) {
            crit.opt_print0 = 1;
        } else if (strcmp(argv[i], "-print") == 0) {
            // default
        } else if (strcmp(argv[i], "-exec") == 0) {
            crit.exec_argv = &argv[i + 1];
            int start = i + 1;
            while (i + 1 < argc && strcmp(argv[i + 1], ";") != 0) {
                i++;
            }
            crit.exec_argc = (i + 1) - start;
            if (i + 1 < argc && strcmp(argv[i + 1], ";") == 0) i++;
        }
        i++;
    }

    if (path_count == 0) {
        paths[path_count++] = ".";
    }

    for (int p = 0; p < path_count; ++p) {
        walk_dir(paths[p], 0, &crit);
    }

    return 0;
}
