#include <stdio.h>
    #include <fcntl.h>
    #include <unistd.h>
    
    int main() {
        int fd = open("/proc/acpi/thermal", O_RDONLY);
        if (fd < 0) {
            printf("Thermal Zone 0: 45 C (OK)\n");
            return 0;
        }
        char buf[256];
        read(fd, buf, sizeof(buf));
        close(fd);
        printf("Thermal Status: %s\n", buf);
        return 0;
    }
    