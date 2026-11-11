#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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
    
    struct nvme_smart_log log;
    if (ioctl(fd, AKAOS_NVME_GET_SMART_LOG, &log) < 0) {
        perror("ioctl (SMART Log)");
        close(fd);
        return 1;
    }
    
    printf("SMART Health Information for %s:\n", argv[1]);
    printf("  Critical Warning: %d\n", log.critical_warning);
    printf("  Temperature: %d K\n", (log.temperature[1] << 8) | log.temperature[0]);
    printf("  Available Spare: %d%%\n", log.available_spare);
    printf("  Percentage Used: %d%%\n", log.percentage_used);
    printf("  Data Units Read: %llu\n", (unsigned long long)log.data_units_read[0]);
    printf("  Data Units Written: %llu\n", (unsigned long long)log.data_units_written[0]);
    printf("  Power On Hours: %llu\n", (unsigned long long)log.power_on_hours[0]);
    printf("  Unsafe Shutdowns: %llu\n", (unsigned long long)log.unsafe_shutdowns[0]);
    printf("  Media Errors: %llu\n", (unsigned long long)log.media_errors[0]);

    close(fd);
    return 0;
}
