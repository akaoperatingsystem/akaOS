#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char gpu_buf[4096];

static void print_usage(void) {
    puts("Usage: gpuinfo [options]\n"
         "Display graphics processing unit (GPU) hardware and driver information.\n\n"
         "Options:\n"
         "  -v, --verbose  be verbose\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  output version information and exit\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("gpuinfo (akaOS XSCore)");
            return 0;
        }
    }

    int fd = open("/proc/gpuinfo", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "gpuinfo: cannot open /proc/gpuinfo\n");
        return 1;
    }
    ssize_t n = read(fd, gpu_buf, sizeof(gpu_buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "gpuinfo: no GPU hardware info available\n");
        return 1;
    }
    gpu_buf[n] = '\0';

    puts("=== akaOS Graphics Processing Unit (GPU) Subsystem ===");
    printf("%s", gpu_buf);

    return 0;
}
