#include <akaos/fs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct find_criteria {
    const char *name_pattern;
    char type_filter; // 'f', 'd', 'c', or 0
    int max_depth;    // -1 for unlimited
    char **exec_argv;
    int exec_argc;
};

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

static int matches_criteria(const char *path, const struct akaos_stat *st,
                            const struct find_criteria *crit) {
    if (crit->type_filter) {
        if (crit->type_filter == 'f' && !S_ISREG(st->mode)) return 0;
        if (crit->type_filter == 'd' && !S_ISDIR(st->mode)) return 0;
        if (crit->type_filter == 'c' && !S_ISCHR(st->mode)) return 0;
    }
    if (crit->name_pattern) {
        const char *bname = get_basename(path);
        if (!glob_match(crit->name_pattern, bname)) return 0;
    }
    return 1;
}

static void execute_on_file(const char *path, const struct find_criteria *crit) {
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
        printf("%s\n", path);
    }
}

static void walk_dir(const char *path, int current_depth,
                     const struct find_criteria *crit) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) return;

    if (matches_criteria(path, &st, crit)) {
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
    printf("Usage: find [PATH...] [EXPRESSION...]\n");
    printf("Search for files in a directory hierarchy.\n\n");
    printf("Expressions:\n");
    printf("  -name PATTERN     Match filename using pattern\n");
    printf("  -type [f|d|c]     Filter by file type (f=regular, d=dir, c=char)\n");
    printf("  -maxdepth N       Descend at most N directory levels\n");
    printf("  -print            Print matching path (default)\n");
    printf("  -exec CMD... {} ; Execute CMD with {} replaced by path\n");
    printf("  -h, --help        Display this help and exit\n");
}

int main(int argc, char **argv) {
    const char *paths[16];
    int path_count = 0;
    struct find_criteria crit;
    crit.name_pattern = NULL;
    crit.type_filter = 0;
    crit.max_depth = -1;
    crit.exec_argv = NULL;
    crit.exec_argc = 0;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
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
        } else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
            crit.type_filter = argv[++i][0];
        } else if (strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc) {
            crit.max_depth = atoi(argv[++i]);
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
