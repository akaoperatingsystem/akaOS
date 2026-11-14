#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <string.h>

#define FORMAT_VERSION "1.0 (akaOS coreutils)"

static void print_usage(void) {
    printf("Usage: format [OPTION]... DEVICE\n"
           "Format a disk or partition.\n\n"
           "Options:\n"
           "  -v, --version       display version information and exit\n"
           "  -h, --help          display this help and exit\n\n"
           "WARNING: Formatting will destroy all data on the specified device.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *device = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("format %s\n", FORMAT_VERSION);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "format: invalid option -- '%s'\n", argv[i]);
            print_usage();
            return 1;
        } else {
            device = argv[i];
        }
    }

    if (!device) {
        fprintf(stderr, "format: no device specified\n");
        print_usage();
        return 1;
    }

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("format: open");
        return 1;
    }

    printf("Formatting device: %s...\n", device);
    
    // Using the same ioctl as fdformat, as it seems to be the system's way to format blocks
    if (ioctl(fd, AKAOS_BLKFORMAT, NULL) < 0) {
        perror("format: ioctl AKAOS_BLKFORMAT");
        close(fd);
        return 1;
    }
    
    printf("Format complete.\n");
    close(fd);
    return 0;
}
