#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* Forward declaration and minimal constant to avoid header mess */
extern int ioctl(int fd, unsigned long request, void *argument);
#define AKAOS_FSCK 0x1279 

static int opt_verbose = 0;
static int opt_interactive = 0;

static void print_usage(void) {
    printf("Usage: fsck [OPTION] [FILESYSTEM]\n"
           "File system consistency check and repair.\n\n"
           "Options:\n"
           "  -r          interactive mode; prompt before repairs\n"
           "  -V          verbose mode\n"
           "  -h, --help  display this help and exit\n");
}

int main(int argc, char *argv[]) {
    int opt;
    const char *device = NULL;

    while ((opt = getopt(argc, argv, "rVh")) != -1) {
        switch (opt) {
            case 'r': opt_interactive = 1; break;
            case 'V': opt_verbose = 1; break;
            case 'h': print_usage(); return 0;
            default: print_usage(); return 1;
        }
    }

    if (optind < argc) {
        device = argv[optind];
    } else {
        fprintf(stderr, "fsck: no filesystem specified\n");
        print_usage();
        return 1;
    }

    if (opt_verbose) printf("Checking filesystem: %s...\n", device);

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("fsck: open");
        return 1;
    }

    // Trigger the filesystem consistency check
    if (ioctl(fd, AKAOS_FSCK, (void *)(uintptr_t)opt_interactive) < 0) {
        perror("fsck: ioctl AKAOS_FSCK");
        close(fd);
        return 1;
    }

    printf("Check complete.\n");
    close(fd);
    return 0;
}
