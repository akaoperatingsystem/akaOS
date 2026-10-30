#include <unistd.h>
#include <string.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        write_str(STDOUT_FILENO, "Usage: sync\nSynchronize cached file data and metadata to storage.\n");
        return 0;
    }
    if (argc != 1) {
        write_str(STDERR_FILENO, "sync: invalid option or extra operand\n");
        return 1;
    }

    if (sync() != 0) {
        write_str(STDERR_FILENO, "sync: error synchronizing storage\n");
        return 1;
    }
    return 0;
}
