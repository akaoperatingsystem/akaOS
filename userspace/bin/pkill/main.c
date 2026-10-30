#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static char proc_buf[1024];

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pkill [-signal] pattern\n");
        return 2;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: pkill [-s signal | -signal] [-e] pattern\n"
               "Signal processes by name pattern.\n\n"
               "Options:\n"
               "  -s <sig>, -<sig>   Signal to send (default: 15 / SIGTERM)\n"
               "  -e                 Display killed process info\n"
               "  -h, --help         Display this help\n");
        return 0;
    }

    int target_sig = SIGTERM;
    int verbose = 0;
    const char *pattern = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-e") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "pkill: -s requires an argument\n");
                return 2;
            }
            target_sig = parse_signal(argv[++i]);
            if (target_sig < 0) {
                fprintf(stderr, "pkill: %s: invalid signal\n", argv[i]);
                return 2;
            }
        } else if (argv[i][0] == '-') {
            target_sig = parse_signal(argv[i] + 1);
            if (target_sig < 0) {
                fprintf(stderr, "pkill: %s: invalid option or signal\n", argv[i]);
                return 2;
            }
        } else {
            pattern = argv[i];
        }
    }

    if (!pattern) {
        fprintf(stderr, "pkill: no pattern specified\n");
        return 2;
    }

    int fd = open("/proc/processes", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "pkill: cannot open /proc/processes\n");
        return 2;
    }

    ssize_t n = read(fd, proc_buf, sizeof(proc_buf) - 1);
    close(fd);
    if (n <= 0) {
        return 1;
    }
    proc_buf[n] = '\0';

    pid_t self = getpid();
    int matched = 0;
    char *line = proc_buf;
    int first_line = 1;

    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        if (first_line) {
            first_line = 0;
        } else if (*line) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p >= '0' && *p <= '9') {
                int pid = atoi(p);
                /* skip ppid, pgid, sid, state to reach name */
                for (int col = 0; col < 5; ++col) {
                    while (*p && *p != ' ' && *p != '\t') p++;
                    while (*p == ' ' || *p == '\t') p++;
                }
                char *name = p;
                /* trim trailing spaces */
                size_t nlen = strlen(name);
                while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\r')) {
                    name[--nlen] = '\0';
                }

                if (pid > 0 && pid != (int)self && strstr(name, pattern) != NULL) {
                    if (kill(pid, target_sig) == 0) {
                        matched++;
                        if (verbose) {
                            printf("pkill: killed %s (pid %d) with signal %d\n", name, pid, target_sig);
                        }
                    } else {
                        fprintf(stderr, "pkill: failed to signal %s (pid %d): %s\n", name, pid, strerror(errno));
                    }
                }
            }
        }

        if (!eol) break;
        line = eol + 1;
    }

    return matched > 0 ? 0 : 1;
}
