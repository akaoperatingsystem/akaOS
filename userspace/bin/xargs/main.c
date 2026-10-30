#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int s_max_args = 64;
static const char *s_replace_str = NULL;
static int s_null_delim = 0;
static int s_trace = 0;

static void print_usage(void) {
    printf("Usage: xargs [OPTIONS] [COMMAND [INITIAL-ARGS...]]\n");
    printf("Build and execute command lines from standard input.\n\n");
    printf("Options:\n");
    printf("  -n MAX-ARGS     use at most MAX-ARGS arguments per command line\n");
    printf("  -I REPLACE-STR  replace occurrences of REPLACE-STR in initial-args\n");
    printf("  -0              items are separated by null, not whitespace\n");
    printf("  -t              print commands before executing them\n");
    printf("  -h, --help      display this help and exit\n");
}

static int run_command(char **argv) {
    if (!argv || !argv[0]) return 0;
    if (s_trace) {
        for (int i = 0; argv[i]; ++i) {
            fprintf(stderr, "%s%s", argv[i], argv[i + 1] ? " " : "\n");
        }
    }
    pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        /* If not found, try resolving /bin/<cmd> */
        if (argv[0][0] != '/') {
            char bin_path[64];
            snprintf(bin_path, sizeof(bin_path), "/bin/%s", argv[0]);
            execv(bin_path, argv);
        }
        exit(127);
    } else if (pid > 0) {
        int status = 0;
        if (waitpid(pid, &status, 0) != pid) return 1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *cmd_name = "/bin/echo";
    char *initial_args[32];
    int init_count = 0;

    int arg_idx = 1;
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "--help") == 0 || strcmp(argv[arg_idx], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[arg_idx], "-0") == 0) {
            s_null_delim = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-t") == 0) {
            s_trace = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-n") == 0 && arg_idx + 1 < argc) {
            s_max_args = atoi(argv[++arg_idx]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-I") == 0 && arg_idx + 1 < argc) {
            s_replace_str = argv[++arg_idx];
            arg_idx++;
        } else if (argv[arg_idx][0] == '-') {
            arg_idx++;
        } else {
            cmd_name = argv[arg_idx++];
            while (arg_idx < argc && init_count < 30) {
                initial_args[init_count++] = argv[arg_idx++];
            }
            break;
        }
    }

    char token[512];
    size_t token_len = 0;
    static char s_read_buf[4096];
    ssize_t n;

    char *cmd_argv[64];
    int cmd_argc = 0;

    /* Base command */
    cmd_argv[cmd_argc++] = (char *)cmd_name;
    for (int i = 0; i < init_count; ++i) {
        cmd_argv[cmd_argc++] = initial_args[i];
    }
    int base_argc = cmd_argc;

    while ((n = read(STDIN_FILENO, s_read_buf, sizeof(s_read_buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char ch = s_read_buf[i];
            int is_delimiter = s_null_delim ? (ch == '\0') : (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');

            if (is_delimiter) {
                if (token_len > 0) {
                    token[token_len] = '\0';
                    char *item = malloc(token_len + 1);
                    if (item) {
                        memcpy(item, token, token_len + 1);

                        if (s_replace_str) {
                            /* Replace placeholder in initial args */
                            char *exec_args[32];
                            int ac = 0;
                            exec_args[ac++] = (char *)cmd_name;
                            for (int k = 0; k < init_count; ++k) {
                                if (strcmp(initial_args[k], s_replace_str) == 0) {
                                    exec_args[ac++] = item;
                                } else {
                                    exec_args[ac++] = initial_args[k];
                                }
                            }
                            exec_args[ac] = NULL;
                            run_command(exec_args);
                            free(item);
                        } else {
                            cmd_argv[cmd_argc++] = item;
                            if (cmd_argc - base_argc >= s_max_args || cmd_argc >= 60) {
                                cmd_argv[cmd_argc] = NULL;
                                run_command(cmd_argv);
                                for (int k = base_argc; k < cmd_argc; ++k) free(cmd_argv[k]);
                                cmd_argc = base_argc;
                            }
                        }
                    }
                    token_len = 0;
                }
            } else {
                if (token_len + 1 < sizeof(token)) {
                    token[token_len++] = ch;
                }
            }
        }
    }

    if (token_len > 0) {
        token[token_len] = '\0';
        char *item = malloc(token_len + 1);
        if (item) {
            memcpy(item, token, token_len + 1);
            if (s_replace_str) {
                char *exec_args[32];
                int ac = 0;
                exec_args[ac++] = (char *)cmd_name;
                for (int k = 0; k < init_count; ++k) {
                    if (strcmp(initial_args[k], s_replace_str) == 0) {
                        exec_args[ac++] = item;
                    } else {
                        exec_args[ac++] = initial_args[k];
                    }
                }
                exec_args[ac] = NULL;
                run_command(exec_args);
                free(item);
            } else {
                cmd_argv[cmd_argc++] = item;
            }
        }
    }

    if (!s_replace_str && cmd_argc > base_argc) {
        cmd_argv[cmd_argc] = NULL;
        run_command(cmd_argv);
        for (int k = base_argc; k < cmd_argc; ++k) free(cmd_argv[k]);
    }

    return 0;
}
