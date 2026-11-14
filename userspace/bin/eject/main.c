#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

/* Eject ioctl, to be implemented in the kernel */
#ifndef AKAOS_EJECT
#define AKAOS_EJECT 0x541C
#endif

static void print_usage(void) {
    printf("Usage: eject [options] [device]\n");
    printf("Options:\n");
    printf("  -h, --help       Display this help\n");
    printf("  -v, --verbose    Verbose mode\n");
    printf("  -r, --cdrom      Eject CDROM (default)\n");
    printf("  -V, --version    Display version\n");
}

int main(int argc, char **argv) {
    char *device = "/dev/cdrom";
    int verbose = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("eject (akaOS coreutils) 1.0\n");
            return 0;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--cdrom") == 0) {
            // Already default
        } else if (argv[i][0] != '-') {
            device = argv[i];
        }
    }
    
    if (verbose) printf("Ejecting %s...\n", device);
    
    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        // Try prepending /dev/
        char full_dev[256];
        snprintf(full_dev, sizeof(full_dev), "/dev/%s", device);
        fd = open(full_dev, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            perror("eject: cannot open device");
            return 1;
        }
    }
    
    // Attempt eject via IOCTL
    if (ioctl(fd, AKAOS_EJECT, NULL) < 0) {
        if (errno == ENOTTY) {
            fprintf(stderr, "eject: %s: operation not supported by device\n", device);
        } else {
            perror("eject: ioctl failed");
        }
        close(fd);
        return 1;
    }
    
    if (verbose) printf("Success.\n");
    close(fd);
    return 0;
}
