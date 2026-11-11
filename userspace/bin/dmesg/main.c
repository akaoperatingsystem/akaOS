#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <akaos/ioctl.h>

#define DMESG_VERSION "2.40.2 (akaOS coreutils)"
#define DEFAULT_BUFSIZE 16392

static void print_version(void) {
    printf("dmesg %s\n", DMESG_VERSION);
}

static void print_help(void) {
    printf("Usage: dmesg [options]\n\n"
           "Display or control the kernel ring buffer.\n\n"
           "Options:\n"
           "  -c          clear the ring buffer contents after printing\n"
           "  -s bufsize  use a buffer of size bufsize to query the kernel ring buffer\n"
           "  -n level    set the level at which logging of messages is done to the console\n"
           "  -h, --help  display this help and exit\n"
           "  -V, --version output version information and exit\n");
}

int main(int argc, char **argv) {
    bool opt_clear = false;
    (void)opt_clear;
    size_t bufsize = DEFAULT_BUFSIZE;
    int console_level = -1;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-c") == 0) {
            opt_clear = true;
        } else if (strcmp(arg, "-s") == 0) {
            if (i + 1 < argc) {
                bufsize = (size_t)strtoul(argv[++i], NULL, 10);
                if (bufsize == 0) bufsize = DEFAULT_BUFSIZE;
            } else {
                fprintf(stderr, "dmesg: option requires an argument -- 's'\n");
                return 1;
            }
        } else if (strcmp(arg, "-n") == 0) {
            if (i + 1 < argc) {
                console_level = atoi(argv[++i]);
            } else {
                fprintf(stderr, "dmesg: option requires an argument -- 'n'\n");
                return 1;
            }
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else {
            fprintf(stderr, "dmesg: invalid option -- '%s'\n", arg);
            fprintf(stderr, "Try 'dmesg --help' for more information.\n");
            return 1;
        }
    }

    if (console_level >= 0) {
        /* When -n option is used, dmesg will not print or clear the kernel ring buffer */
        return 0;
    }

    int fd = open("/proc/kmsg", O_RDONLY);
    if (fd < 0) {
        perror("dmesg: read kernel buffer failed");
        return 1;
    }

    if (opt_clear) {
        ioctl(fd, AKAOS_DMESG_CLEAR, NULL);
    }


    char *buf = malloc(bufsize);
    if (!buf) {
        fprintf(stderr, "dmesg: memory allocation failed\n");
        close(fd);
        return 1;
    }

    ssize_t total = 0;
    ssize_t n;
    while ((n = read(fd, buf + total, bufsize - total - 1)) > 0) {
        total += n;
        if ((size_t)total >= bufsize - 1) break;
    }
    close(fd);

    if (total > 0) {
        buf[total] = '\0';
        fputs(buf, stdout);
        if (buf[total - 1] != '\n') {
            putchar('\n');
        }
    }

    free(buf);
    return 0;
}
