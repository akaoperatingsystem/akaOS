#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode) && (st.st_mode & 0111)) {
            return 1;
        }
    }
    return 0;
}

static int find_in_path(const char *cmd, const char *search_path, char *out_path, size_t out_sz) {
    if (!cmd || !*cmd) return 0;
    if (strchr(cmd, '/')) {
        if (is_executable(cmd)) {
            strncpy(out_path, cmd, out_sz - 1);
            out_path[out_sz - 1] = '\0';
            return 1;
        }
        return 0;
    }
    if (!search_path || !*search_path) search_path = "/bin:/usr/bin";

    char sp[512];
    strncpy(sp, search_path, sizeof(sp) - 1);
    sp[sizeof(sp) - 1] = '\0';

    char *tok = strtok(sp, ":");
    while (tok) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s/%s", tok, cmd);
        if (is_executable(buf)) {
            strncpy(out_path, buf, out_sz - 1);
            out_path[out_sz - 1] = '\0';
            return 1;
        }
        tok = strtok(NULL, ":");
    }
    return 0;
}

int main(int argc, char **argv) {
    int opt_p = 0;
    int opt_v = 0;
    int opt_V = 0;

    int idx = 1;
    while (idx < argc && argv[idx][0] == '-' && argv[idx][1] != '\0') {
        if (strcmp(argv[idx], "--") == 0) {
            idx++;
            break;
        }
        for (int k = 1; argv[idx][k]; ++k) {
            if (argv[idx][k] == 'p') opt_p = 1;
            else if (argv[idx][k] == 'v') opt_v = 1;
            else if (argv[idx][k] == 'V') opt_V = 1;
            else {
                fprintf(stderr, "command: invalid option -- '%c'\n", argv[idx][k]);
                return 2;
            }
        }
        idx++;
    }

    if (idx >= argc) {
        return 0;
    }

    const char *custom_path = opt_p ? "/bin:/usr/bin" : getenv("PATH");
    if (!custom_path) custom_path = "/bin:/usr/bin";

    if (opt_v || opt_V) {
        int not_found = 0;
        for (int i = idx; i < argc; ++i) {
            char found_path[256];
            if (find_in_path(argv[i], custom_path, found_path, sizeof(found_path))) {
                if (opt_V) {
                    printf("%s is %s\n", argv[i], found_path);
                } else {
                    printf("%s\n", found_path);
                }
            } else {
                if (opt_V) {
                    fprintf(stderr, "command: %s: not found\n", argv[i]);
                }
                not_found = 1;
            }
        }
        return not_found ? 1 : 0;
    }

    if (opt_p) {
        setenv("PATH", "/bin:/usr/bin", 1);
    }

    execvp(argv[idx], &argv[idx]);
    int err = errno;
    fprintf(stderr, "command: %s: %s\n", argv[idx], strerror(err));
    return (err == ENOENT) ? 127 : 126;
}
