#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: ps\nReport current XSCore processes.");
        return 0;
    }
    if (argc != 1) {
        puts("ps: unsupported option or extra operand");
        return 2;
    }
    int descriptor = open("/proc/processes", O_RDONLY);
    if (descriptor < 0) {
        puts("ps: cannot open /proc/processes");
        return 1;
    }
    char buffer[256];
    ssize_t count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        if (write(STDOUT_FILENO, buffer, (size_t)count) != count) {
            close(descriptor);
            return 1;
        }
    }
    close(descriptor);
    return count < 0 ? 1 : 0;
}
