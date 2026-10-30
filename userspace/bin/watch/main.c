#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static char cmd_line[256];

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: watch [-n sec] command...\n");
        return 1;
    }

    int interval_sec = 2;
    int max_count = 0; /* 0 = infinite */
    int cmd_start = 1;

    while (cmd_start < argc && argv[cmd_start][0] == '-') {
        if (strcmp(argv[cmd_start], "-h") == 0 || strcmp(argv[cmd_start], "--help") == 0) {
            printf("Usage: watch [options] command\n"
                   "Execute a program periodically, showing output fullscreen.\n\n"
                   "Options:\n"
                   "  -n, --interval <secs>   Seconds to wait between updates (default: 2)\n"
                   "  -c, --count <number>    Exit after N executions\n"
                   "  -h, --help              Display this help and exit\n");
            return 0;
        } else if (strcmp(argv[cmd_start], "-n") == 0 || strcmp(argv[cmd_start], "--interval") == 0) {
            if (cmd_start + 1 >= argc) {
                fprintf(stderr, "watch: option requires an argument -- '%s'\n", argv[cmd_start]);
                return 1;
            }
            interval_sec = atoi(argv[++cmd_start]);
            if (interval_sec < 1) interval_sec = 1;
            cmd_start++;
        } else if (strcmp(argv[cmd_start], "-c") == 0 || strcmp(argv[cmd_start], "--count") == 0) {
            if (cmd_start + 1 >= argc) {
                fprintf(stderr, "watch: option requires an argument -- '%s'\n", argv[cmd_start]);
                return 1;
            }
            max_count = atoi(argv[++cmd_start]);
            cmd_start++;
        } else {
            break;
        }
    }

    if (cmd_start >= argc) {
        fprintf(stderr, "watch: no command specified\n");
        return 1;
    }

    /* Build command line string */
    cmd_line[0] = '\0';
    size_t len = 0;
    for (int i = cmd_start; i < argc; ++i) {
        size_t arg_len = strlen(argv[i]);
        if (len + arg_len + 2 < sizeof(cmd_line)) {
            if (len > 0) cmd_line[len++] = ' ';
            memcpy(cmd_line + len, argv[i], arg_len);
            len += arg_len;
            cmd_line[len] = '\0';
        }
    }

    signal(SIGINT, handle_sigint);

    int count = 0;
    while (running) {
        /* Clear screen and move cursor home */
        printf("\033[2J\033[H");
        printf("Every %ds: %s\n\n", interval_sec, cmd_line);

        pid_t pid = fork();
        if (pid == 0) {
            const char *args[] = {"/bin/akash", "-c", cmd_line, NULL};
            execve("/bin/akash", (char *const *)args, NULL);
            _exit(127);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }

        count++;
        if (max_count > 0 && count >= max_count) {
            break;
        }

        /* Sleep interval in chunks so SIGINT is responsive */
        for (int s = 0; s < interval_sec * 10 && running; ++s) {
            usleep(100000);
        }
    }

    return 0;
}
