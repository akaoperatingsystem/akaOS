#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void write_str(int fd, const char *s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, s + written, len - written);
        if (res <= 0) break;
        written += (size_t)res;
    }
}

static void print_usage(void) {
    write_str(STDOUT_FILENO, "Usage: chmod [OPTION]... MODE[,MODE]... FILE...\n");
    write_str(STDOUT_FILENO, "Change the mode of each FILE to MODE.\n\n");
    write_str(STDOUT_FILENO, "  -h, --help     display this help and exit\n\n");
    write_str(STDOUT_FILENO, "MODE is either an octal number (e.g. 755, 644) or symbolic (e.g. +x, u+rw, go-w).\n");
}

static int parse_octal_mode(const char *str, mode_t *out_mode) {
    if (!str || !*str) return 0;
    mode_t mode = 0;
    for (size_t i = 0; str[i]; ++i) {
        if (str[i] < '0' || str[i] > '7') return 0;
        mode = (mode << 3) | (mode_t)(str[i] - '0');
    }
    if (out_mode) *out_mode = mode;
    return 1;
}

static int parse_symbolic_mode(const char *str, mode_t current_mode, mode_t *out_mode) {
    if (!str || !*str) return 0;
    mode_t mode = current_mode & 07777;
    const char *p = str;

    while (*p) {
        mode_t who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u') who |= 0700;
            if (*p == 'g') who |= 0070;
            if (*p == 'o') who |= 0007;
            if (*p == 'a') who |= 0777;
            ++p;
        }
        if (who == 0) who = 0777;

        if (*p != '+' && *p != '-' && *p != '=') return 0;
        char op = *p++;

        mode_t perm_bits = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 's' || *p == 't') {
            if (*p == 'r') perm_bits |= (S_IRUSR | S_IRGRP | S_IROTH);
            if (*p == 'w') perm_bits |= (S_IWUSR | S_IWGRP | S_IWOTH);
            if (*p == 'x') perm_bits |= (S_IXUSR | S_IXGRP | S_IXOTH);
            if (*p == 's') perm_bits |= (S_ISUID | S_ISGID);
            if (*p == 't') perm_bits |= S_ISVTX;
            ++p;
        }

        mode_t mask = perm_bits & who;
        if (op == '+') {
            mode |= mask;
        } else if (op == '-') {
            mode &= ~mask;
        } else if (op == '=') {
            mode = (mode & ~who) | mask;
        }

        if (*p == ',') ++p;
    }

    if (out_mode) *out_mode = mode;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        write_str(STDERR_FILENO, "chmod: missing operand\n");
        write_str(STDERR_FILENO, "Try 'chmod --help' for more information.\n");
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    if (argc < 3) {
        write_str(STDERR_FILENO, "chmod: missing operand after '");
        write_str(STDERR_FILENO, argv[1]);
        write_str(STDERR_FILENO, "'\n");
        write_str(STDERR_FILENO, "Try 'chmod --help' for more information.\n");
        return 1;
    }

    const char *mode_str = argv[1];
    mode_t octal_val = 0;
    int is_octal = parse_octal_mode(mode_str, &octal_val);

    int exit_status = 0;
    for (int i = 2; i < argc; ++i) {
        const char *file_path = argv[i];
        mode_t final_mode = 0;

        if (is_octal) {
            final_mode = octal_val;
        } else {
            struct akaos_stat st;
            if (stat(file_path, &st) < 0) {
                write_str(STDERR_FILENO, "chmod: cannot access '");
                write_str(STDERR_FILENO, file_path);
                write_str(STDERR_FILENO, "': No such file or directory\n");
                exit_status = 1;
                continue;
            }
            if (!parse_symbolic_mode(mode_str, (mode_t)st.mode, &final_mode)) {
                write_str(STDERR_FILENO, "chmod: invalid mode: '");
                write_str(STDERR_FILENO, mode_str);
                write_str(STDERR_FILENO, "'\n");
                return 1;
            }
        }

        if (chmod(file_path, final_mode) < 0) {
            write_str(STDERR_FILENO, "chmod: changing permissions of '");
            write_str(STDERR_FILENO, file_path);
            if (errno == ENOENT) {
                write_str(STDERR_FILENO, "': No such file or directory\n");
            } else if (errno == EROFS) {
                write_str(STDERR_FILENO, "': Read-only file system\n");
            } else {
                write_str(STDERR_FILENO, "': Operation failed\n");
            }
            exit_status = 1;
        }
    }

    return exit_status;
}
