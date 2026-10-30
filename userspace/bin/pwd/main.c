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
        write_str(STDOUT_FILENO, "Usage: pwd\nPrint the current working directory.\n");
        return 0;
    }
    if (argc != 1) {
        write_str(STDERR_FILENO, "pwd: invalid option or extra operand\n");
        return 1;
    }
    char buffer[128];
    if (getcwd(buffer, sizeof(buffer)) != 0) {
        write_str(STDOUT_FILENO, buffer);
        write_str(STDOUT_FILENO, "\n");
        return 0;
    }
    write_str(STDERR_FILENO, "pwd: error getting current directory\n");
    return 1;
}
