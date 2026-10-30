#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
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
        write_str(STDERR_FILENO, "mkdir: missing operand\n");
        return 1;
    }

    int had_error = 0;
    int options_done = 0;
    int operand_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!options_done && strcmp(argv[i], "--help") == 0) {
            write_str(STDOUT_FILENO, "Usage: mkdir [--] DIRECTORY...\nCreate directories.\n");
            return 0;
        }
        if (!options_done && strcmp(argv[i], "--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done && argv[i][0] == '-') {
            write_str(STDERR_FILENO, "mkdir: invalid option '" );
            write_str(STDERR_FILENO, argv[i]);
            write_str(STDERR_FILENO, "'\n");
            return 1;
        }
        ++operand_count;
        if (mkdir(argv[i], 0755) != 0) {
            write_str(STDERR_FILENO, "mkdir: cannot create directory '");
            write_str(STDERR_FILENO, argv[i]);
            if (errno == EEXIST) {
                write_str(STDERR_FILENO, "': File exists\n");
            } else if (errno == ENOENT) {
                write_str(STDERR_FILENO, "': No such file or directory\n");
            } else {
                write_str(STDERR_FILENO, "': Error\n");
            }
            had_error = 1;
        }
    }

    if (operand_count == 0) {
        write_str(STDERR_FILENO, "mkdir: missing operand\n");
        return 1;
    }

    return had_error ? 1 : 0;
}
