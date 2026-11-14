#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <akaos/ioctl.h>
#include <akaos/nvme.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: diskinfo <device>\n");
        return 1;
    }
    
    // In a real system, we'd query sysfs or ioctls to get device info.
    // Given our current implementation, we can simulate by printing the 
    // basic information derived from the device node path.
    
    printf("Device: %s\n", argv[1]);
    // Simulate/Placeholder for now
    printf("Model: Generic Storage Device\n");
    printf("Interface: NVMe/SATA\n");
    
    return 0;
}
