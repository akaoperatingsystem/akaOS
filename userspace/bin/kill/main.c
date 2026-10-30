#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sig_map {
    int num;
    char name[8];
};

static const struct sig_map sig_table[] = {
    {1, "HUP"},
    {2, "INT"},
    {3, "QUIT"},
    {4, "ILL"},
    {5, "TRAP"},
    {6, "ABRT"},
    {7, "BUS"},
    {8, "FPE"},
    {9, "KILL"},
    {10, "USR1"},
    {11, "SEGV"},
    {12, "USR2"},
    {13, "PIPE"},
    {14, "ALRM"},
    {15, "TERM"},
    {17, "CHLD"},
    {18, "CONT"},
    {19, "STOP"},
    {20, "TSTP"},
    {21, "TTIN"},
    {22, "TTOU"},
    {0, ""}
};

static int parse_signal(const char *s) {
    if (!s || !*s) return -1;
    if (s[0] == 'S' && s[1] == 'I' && s[2] == 'G') {
        s += 3;
    }
    int is_num = 1;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            is_num = 0;
            break;
        }
    }
    if (is_num) {
        int n = atoi(s);
        if (n >= 1 && n <= 31) return n;
        return -1;
    }
    for (size_t i = 0; sig_table[i].num != 0; ++i) {
        const char *name = sig_table[i].name;
        size_t j = 0;
        int match = 1;
        while (name[j] || s[j]) {
            char c1 = name[j];
            char c2 = s[j];
            if (c2 >= 'a' && c2 <= 'z') c2 = (char)(c2 - 'a' + 'A');
            if (c1 != c2) { match = 0; break; }
            j++;
        }
        if (match) return sig_table[i].num;
    }
    return -1;
}

static void print_signals(void) {
    for (size_t i = 0; sig_table[i].num != 0; ++i) {
        printf("%2d) SIG%-6s", sig_table[i].num, sig_table[i].name);
        if ((i + 1) % 4 == 0 || sig_table[i + 1].num == 0) {
            printf("\n");
        } else {
            printf(" ");
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "kill: usage: kill [-s sigspec | -n signum | -sigspec] pid | [ -l [signum] ]\n");
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: kill [-s signal | -signal] PID...\n"
               "       kill -l [signal]\n"
               "Send signals to processes.\n\n"
               "Options:\n"
               "  -s <sig>, -<sig>   Specify signal to send (name or number, default: 15 / SIGTERM)\n"
               "  -l [sig]           List signal names or convert signal number to name\n"
               "  -h, --help         Display this help and exit\n");
        return 0;
    }

    if (strcmp(argv[1], "-l") == 0) {
        if (argc == 2) {
            print_signals();
            return 0;
        }
        int sig = parse_signal(argv[2]);
        if (sig < 0) {
            fprintf(stderr, "kill: %s: unknown signal\n", argv[2]);
            return 1;
        }
        for (size_t i = 0; sig_table[i].num != 0; ++i) {
            if (sig_table[i].num == sig) {
                printf("%s\n", sig_table[i].name);
                return 0;
            }
        }
        printf("%d\n", sig);
        return 0;
    }

    int target_sig = SIGTERM;
    int arg_index = 1;

    if (argv[1][0] == '-') {
        if (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "-n") == 0) {
            if (argc < 4) {
                fprintf(stderr, "kill: %s requires an argument\n", argv[1]);
                return 1;
            }
            target_sig = parse_signal(argv[2]);
            if (target_sig < 0) {
                fprintf(stderr, "kill: %s: invalid signal\n", argv[2]);
                return 1;
            }
            arg_index = 3;
        } else {
            target_sig = parse_signal(argv[1] + 1);
            if (target_sig < 0) {
                fprintf(stderr, "kill: %s: invalid signal\n", argv[1]);
                return 1;
            }
            arg_index = 2;
        }
    }

    if (arg_index >= argc) {
        fprintf(stderr, "kill: no process specified\n");
        return 1;
    }

    int exit_code = 0;
    for (int i = arg_index; i < argc; ++i) {
        char *endptr = NULL;
        long pid_val = strtol(argv[i], &endptr, 10);
        if (!argv[i][0] || (endptr && *endptr != '\0') || pid_val <= 0) {
            fprintf(stderr, "kill: %s: arguments must be process IDs\n", argv[i]);
            exit_code = 1;
            continue;
        }
        if (kill((int)pid_val, target_sig) != 0) {
            fprintf(stderr, "kill: (%ld) - %s\n", pid_val, strerror(errno));
            exit_code = 1;
        }
    }

    return exit_code;
}
