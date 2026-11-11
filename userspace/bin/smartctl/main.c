#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <akaos/ioctl.h>
#include <akaos/nvme.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: smartctl <device>\n");
        return 1;
    }
    
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Attempt NVMe SMART Log
    struct nvme_smart_log log;
    if (ioctl(fd, AKAOS_NVME_GET_SMART_LOG, &log) == 0) {
        printf("SMART Health Information for %s (NVMe):\n", argv[1]);
        printf("  Critical Warning: %d\n", log.critical_warning);
        printf("  Temperature: %d K\n", (log.temperature[1] << 8) | log.temperature[0]);
        printf("  Percentage Used: %d%%\n", log.percentage_used);
        // ... (other fields)
    } else {
        // Attempt SATA SMART (Placeholder)
        printf("SMART Health Information for %s (SATA/Other):\n", argv[1]);
        printf("  Detection failed or device not supported yet.\n");
    }
    
    close(fd);
    return 0;
}
