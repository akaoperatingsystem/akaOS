
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <akaos/ioctl.h>
#include <akaos/display.h>

int main() {
    printf("akaOS Graphics Diagnostics Tool\n");
    int fd = open("/dev/gpu0", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open /dev/gpu0\n");
        return 1;
    }
    printf("GPU /dev/gpu0 opened successfully.\n");
    
    // Test IOCTLs
    if (ioctl(fd, DRM_IOCTL_VERSION) == 0) {
        printf("DRM version query success.\n");
    } else {
        printf("DRM version query failed.\n");
    }
    
    close(fd);
    return 0;
}
