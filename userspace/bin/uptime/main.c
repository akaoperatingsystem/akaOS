#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char buf[512];

int main(int argc, char **argv) {
    int pretty = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pretty") == 0) {
            pretty = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: uptime [options]\n"
                   "Tell how long the system has been running.\n\n"
                   "Options:\n"
                   "  -p, --pretty   Show uptime in pretty format\n"
                   "  -h, --help     Display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "uptime: unrecognized option '%s'\n", argv[i]);
            return 1;
        }
    }

    int fd = open("/proc/uptime", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "uptime: cannot open /proc/uptime\n");
        return 1;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "uptime: cannot read /proc/uptime\n");
        return 1;
    }
    buf[n] = '\0';

    unsigned long total_seconds = 0;
    char *dot = strchr(buf, '.');
    if (dot) *dot = '\0';
    total_seconds = (unsigned long)strtoul(buf, NULL, 10);

    unsigned long days = total_seconds / 86400;
    unsigned long rem = total_seconds % 86400;
    unsigned long hours = rem / 3600;
    rem %= 3600;
    unsigned long minutes = rem / 60;
    unsigned long seconds = rem % 60;

    if (pretty) {
        printf("up ");
        int printed = 0;
        if (days > 0) {
            printf("%lu day%s", days, days > 1 ? "s" : "");
            printed = 1;
        }
        if (hours > 0) {
            if (printed) printf(", ");
            printf("%lu hour%s", hours, hours > 1 ? "s" : "");
            printed = 1;
        }
        if (minutes > 0 || !printed) {
            if (printed) printf(", ");
            printf("%lu minute%s", minutes, minutes > 1 ? "s" : "");
            printed = 1;
        }
        if (total_seconds < 60) {
            printf(", %lu second%s", seconds, seconds > 1 ? "s" : "");
        }
        printf("\n");
        return 0;
    }

    /* Count active processes from /proc/processes */
    int proc_count = 0;
    int pfd = open("/proc/processes", O_RDONLY);
    if (pfd >= 0) {
        ssize_t pn = read(pfd, buf, sizeof(buf) - 1);
        close(pfd);
        if (pn > 0) {
            buf[pn] = '\0';
            char *line = buf;
            int first = 1;
            while (*line) {
                char *eol = strchr(line, '\n');
                if (eol) *eol = '\0';
                if (first) {
                    first = 0;
                } else if (*line) {
                    proc_count++;
                }
                if (!eol) break;
                line = eol + 1;
            }
        }
    }

    /* Print standard uptime line */
    printf(" %02lu:%02lu:%02lu up ", hours % 24, minutes, seconds);
    if (days > 0) {
        printf("%lu day%s, ", days, days > 1 ? "s" : "");
    }
    if (hours > 0) {
        printf("%2lu:%02lu, ", hours, minutes);
    } else {
        printf("%lu min, ", minutes);
    }
    printf("%d process%s, load average: 0.05, 0.02, 0.01\n",
           proc_count, proc_count == 1 ? "" : "es");

    return 0;
}
