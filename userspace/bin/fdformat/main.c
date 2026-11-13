
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <akaos/ioctl.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: fdformat device\n");
        return 1;
    }
    
    const char *device = argv[1];
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("fdformat: open");
        return 1;
    }
    
    printf("Formatting floppy device: %s...\n", device);
    if (ioctl(fd, AKAOS_BLKFORMAT, NULL) < 0) {
        perror("fdformat: ioctl AKAOS_BLKFORMAT");
        close(fd);
        return 1;
    }
    
    printf("Format complete.\n");
    close(fd);
    return 0;
}
