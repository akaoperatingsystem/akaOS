#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_ENV 256
static char *custom_env[MAX_ENV];
static int env_count = 0;

static void print_usage(void) {
    printf("Usage: env [OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]\n");
    printf("Set each NAME to VALUE in the environment and run COMMAND.\n\n");
    printf("Options:\n");
    printf("  -i, --ignore-environment  start with an empty environment\n");
    printf("  -u, --unset=NAME          remove variable from the environment\n");
    printf("  -h, --help                display this help and exit\n");
}

static void env_unset(const char *name) {
    size_t nlen = strlen(name);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(custom_env[i], name, nlen) == 0 && custom_env[i][nlen] == '=') {
            for (int j = i; j < env_count - 1; j++) {
                custom_env[j] = custom_env[j + 1];
            }
            env_count--;
            custom_env[env_count] = NULL;
            i--;
        }
    }
}

static void env_set(char *entry) {
    char *eq = strchr(entry, '=');
    if (!eq) return;
    size_t nlen = (size_t)(eq - entry);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(custom_env[i], entry, nlen) == 0 && custom_env[i][nlen] == '=') {
            custom_env[i] = entry;
            return;
        }
    }
    if (env_count < MAX_ENV - 1) {
        custom_env[env_count++] = entry;
        custom_env[env_count] = NULL;
    }
}

int main(int argc, char *argv[]) {
    int idx = 1;

    /* Copy existing environment by default */
    if (environ) {
        for (char **ep = environ; *ep && env_count < MAX_ENV - 1; ++ep) {
            custom_env[env_count++] = *ep;
        }
    }
    custom_env[env_count] = NULL;

    while (idx < argc) {
        if (strcmp(argv[idx], "-h") == 0 || strcmp(argv[idx], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[idx], "-i") == 0 || strcmp(argv[idx], "--ignore-environment") == 0 || strcmp(argv[idx], "-") == 0) {
            env_count = 0;
            custom_env[0] = NULL;
            idx++;
        } else if (strncmp(argv[idx], "--unset=", 8) == 0) {
            env_unset(argv[idx] + 8);
            idx++;
        } else if (strcmp(argv[idx], "-u") == 0) {
            if (idx + 1 >= argc) {
                fprintf(stderr, "env: option requires an argument -- 'u'\n");
                return 1;
            }
            env_unset(argv[idx + 1]);
            idx += 2;
        } else if (argv[idx][0] == '-' && argv[idx][1] == 'u') {
            env_unset(argv[idx] + 2);
            idx++;
        } else if (strchr(argv[idx], '=') != NULL) {
            env_set(argv[idx]);
            idx++;
        } else {
            /* Start of command */
            break;
        }
    }

    if (idx >= argc) {
        /* No command: print environment */
        for (int i = 0; i < env_count; i++) {
            if (custom_env[i]) {
                printf("%s\n", custom_env[i]);
            }
        }
        return 0;
    }

    /* Command specified */
    const char *cmd = argv[idx];
    char resolved[256];
    const char *target = cmd;

    if (!strchr(cmd, '/')) {
        snprintf(resolved, sizeof(resolved), "/bin/%s", cmd);
        struct akaos_stat st;
        if (stat(resolved, &st) == 0) {
            target = resolved;
        }
    }

    execve(target, &argv[idx], custom_env);

    fprintf(stderr, "env: '%s': %s\n", cmd, strerror(errno));
    return 127;
}
