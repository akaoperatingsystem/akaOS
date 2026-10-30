#include <fcntl.h>
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
    if (argc <= 1) {
        write_str(STDERR_FILENO, "touch: missing file operand\n");
        return 1;
    }

    int had_error = 0;
    int options_done = 0;
    int operand_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!options_done && strcmp(argv[i], "--help") == 0) {
            write_str(STDOUT_FILENO, "Usage: touch [--] FILE...\nCreate empty files.\n");
            return 0;
        }
        if (!options_done && strcmp(argv[i], "--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done && argv[i][0] == '-') {
            write_str(STDERR_FILENO, "touch: invalid option '");
            write_str(STDERR_FILENO, argv[i]);
            write_str(STDERR_FILENO, "'\n");
            return 1;
        }
        ++operand_count;
        int fd = open(argv[i], O_WRONLY | O_CREAT);
        if (fd < 0) {
            write_str(STDERR_FILENO, "touch: cannot touch '");
            write_str(STDERR_FILENO, argv[i]);
            write_str(STDERR_FILENO, "': Error\n");
            had_error = 1;
        } else {
            close(fd);
        }
    }

    if (operand_count == 0) {
        write_str(STDERR_FILENO, "touch: missing file operand\n");
        return 1;
    }

    return had_error ? 1 : 0;
}
