#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char snd_buf[4096];

static void print_usage(void) {
    puts("Usage: sndinfo [options]\n"
         "Display sound hardware, audio cards, and PCM driver information.\n\n"
         "Options:\n"
         "  -v, --verbose  be verbose\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  output version information and exit\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("sndinfo (akaOS XSCore)");
            return 0;
        }
    }

    int fd = open("/proc/asound/cards", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "sndinfo: cannot open /proc/asound/cards\n");
        return 1;
    }
    ssize_t n = read(fd, snd_buf, sizeof(snd_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "sndinfo: no audio hardware detected\n");
        return 1;
    }
    snd_buf[n] = '\0';

    puts("=== akaOS Audio Subsystem & Sound Devices ===");
    printf("%s", snd_buf);

    return 0;
}
