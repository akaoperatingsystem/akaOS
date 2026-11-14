
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"list", no_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "lh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'l': {
            int fd = open("/dev/efivar", O_RDONLY);
            if (fd < 0) {
                perror("Unable to open /dev/efivar");
                return 1;
            }
            char buffer[4096];
            ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                printf("%s", buffer);
            }
            close(fd);
            break;
        }
        case 'h':
            printf("Usage: efivar [options]\n  -l, --list    List all NVRAM variables\n");
            return 0;
        default:
            return 1;
        }
    }
    return 0;
}
