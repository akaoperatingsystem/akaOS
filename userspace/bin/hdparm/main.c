#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <akaos/ioctl.h>
#include <xscore/storage.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: hdparm <option> <device>\n");
        printf("Options:\n  -I: Get ID\n  -C: Check Power Mode\n  -y: Standby\n  -Y: Sleep\n");
        return 1;
    }
    
    char *option = argv[1];
    char *device = argv[2];
    
    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    if (strcmp(option, "-C") == 0) {
        uint8_t mode = 0;
        if (ioctl(fd, AKAOS_DISK_POWER_MODE, &mode) == 0) {
            printf("Power mode: %d\n", mode);
        } else {
            perror("ioctl (check power mode)");
        }
    } else if (strcmp(option, "-y") == 0) {
        uint8_t mode = 1; // Standby
        if (ioctl(fd, AKAOS_DISK_POWER_MODE, &mode) == 0) {
            printf("Set standby mode.\n");
        } else {
            perror("ioctl (set standby)");
        }
    } else if (strcmp(option, "-Y") == 0) {
        uint8_t mode = 2; // Sleep
        if (ioctl(fd, AKAOS_DISK_POWER_MODE, &mode) == 0) {
            printf("Set sleep mode.\n");
        } else {
            perror("ioctl (set sleep)");
        }
    } else {
        printf("Option %s not implemented.\n", option);
    }
    
    close(fd);
    return 0;
}
