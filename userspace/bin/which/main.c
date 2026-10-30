#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_usage(void) {
    printf("Usage: which [options] [--] program [program ...]\n");
    printf("Locate a program file in the user's path.\n\n");
    printf("Options:\n");
    printf("  -a             print all matching pathnames of each argument\n");
    printf("  -h, --help     display this help and exit\n");
}

static int is_executable(const char *path) {
    struct akaos_stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISDIR(st.mode)) return 0;
    if (S_ISREG(st.mode)) return 1;
    return 0;
}

static int find_program(const char *prog, int all, const char *path_env) {
    if (!prog || !*prog) return 0;

    if (strchr(prog, '/')) {
        if (is_executable(prog)) {
            printf("%s\n", prog);
            return 1;
        }
        return 0;
    }

    char path_buf[512];
    if (!path_env || !*path_env) {
        path_env = "/bin:/usr/bin";
    }
    size_t plen = strlen(path_env);
    if (plen >= sizeof(path_buf)) plen = sizeof(path_buf) - 1;
    memcpy(path_buf, path_env, plen);
    path_buf[plen] = '\0';

    int found = 0;
    char *dir = path_buf;
    while (*dir) {
        char *colon = strchr(dir, ':');
        if (colon) *colon = '\0';

        char full_path[256];
        size_t dlen = strlen(dir);
        size_t prlen = strlen(prog);
        if (dlen + 1 + prlen < sizeof(full_path)) {
            memcpy(full_path, dir, dlen);
            full_path[dlen] = '/';
            memcpy(full_path + dlen + 1, prog, prlen + 1);

            if (is_executable(full_path)) {
                printf("%s\n", full_path);
                found = 1;
                if (!all) return 1;
            }
        }

        if (!colon) break;
        dir = colon + 1;
    }

    return found;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int all = 0;
    int first_arg = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-a") == 0) {
            all = 1;
        } else if (strcmp(argv[i], "--") == 0) {
            first_arg = i + 1;
            break;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "which: unknown option '%s'\n", argv[i]);
            print_usage();
            return 1;
        } else {
            first_arg = i;
            break;
        }
    }

    if (first_arg >= argc) {
        print_usage();
        return 1;
    }

    const char *path_env = getenv("PATH");
    int any_missing = 0;

    for (int i = first_arg; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            all = 1;
            continue;
        }
        if (!find_program(argv[i], all, path_env)) {
            any_missing = 1;
        }
    }

    return any_missing ? 1 : 0;
}
