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
    int no_newline = 0;
    int first_arg = 1;
    if (first_arg < argc && strcmp(argv[first_arg], "--help") == 0) {
        write_str(STDOUT_FILENO, "Usage: echo [-n] [STRING]...\n");
        write_str(STDOUT_FILENO, "Write arguments separated by spaces.\n");
        return 0;
    }
    if (first_arg < argc && strcmp(argv[first_arg], "--version") == 0) {
        write_str(STDOUT_FILENO, "echo (akaOS coreutils) 1.0\n");
        return 0;
    }
    if (first_arg < argc && strcmp(argv[first_arg], "--") == 0) {
        first_arg++;
    } else if (first_arg < argc && strcmp(argv[first_arg], "-n") == 0) {
        no_newline = 1;
        first_arg++;
    }

    for (int i = first_arg; i < argc; ++i) {
        if (i > first_arg) {
            write_str(STDOUT_FILENO, " ");
        }
        write_str(STDOUT_FILENO, argv[i]);
    }
    if (!no_newline) write_str(STDOUT_FILENO, "\n");
    return 0;
}
