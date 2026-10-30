#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_error(const char *target, const char *msg) {
    fprintf(stderr, "head: %s: %s\n", target, msg);
}

static int head_stream_lines(int fd, const char *name, int max_lines) {
    if (max_lines <= 0) return 0;
    char buffer[512];
    int lines_printed = 0;

    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            print_error(name, errno == EISDIR ? "Is a directory" : "Read error");
            return 1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            putchar(c);
            if (c == '\n') {
                lines_printed++;
                if (lines_printed >= max_lines) {
                    return 0;
                }
            }
        }
    }
    return 0;
}

static int head_stream_bytes(int fd, const char *name, int max_bytes) {
    if (max_bytes <= 0) return 0;
    char buffer[512];
    int bytes_printed = 0;

    while (bytes_printed < max_bytes) {
        int to_read = sizeof(buffer);
        if (to_read > max_bytes - bytes_printed) {
            to_read = max_bytes - bytes_printed;
        }
        ssize_t n = read(fd, buffer, (size_t)to_read);
        if (n < 0) {
            print_error(name, errno == EISDIR ? "Is a directory" : "Read error");
            return 1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            putchar(buffer[i]);
        }
        bytes_printed += (int)n;
    }
    return 0;
}

static int head_file(const char *path, int max_lines, int max_bytes, int is_bytes) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EISDIR) {
            print_error(path, "Is a directory");
        } else {
            print_error(path, "Cannot open file");
        }
        return 1;
    }

    int res = is_bytes ? head_stream_bytes(fd, path, max_bytes)
                       : head_stream_lines(fd, path, max_lines);
    close(fd);
    return res;
}

int main(int argc, char **argv) {
    int max_lines = 10;
    int max_bytes = 0;
    int is_bytes = 0;
    const char *files[32];
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            if (argv[i][1] == 'n') {
                if (argv[i][2] != '\0') {
                    max_lines = atoi(&argv[i][2]);
                } else if (i + 1 < argc) {
                    max_lines = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "head: option requires an argument -- 'n'\n");
                    return 1;
                }
                is_bytes = 0;
            } else if (argv[i][1] == 'c') {
                if (argv[i][2] != '\0') {
                    max_bytes = atoi(&argv[i][2]);
                } else if (i + 1 < argc) {
                    max_bytes = atoi(argv[++i]);
                } else {
                    fprintf(stderr, "head: option requires an argument -- 'c'\n");
                    return 1;
                }
                is_bytes = 1;
            } else if (argv[i][1] >= '0' && argv[i][1] <= '9') {
                max_lines = atoi(&argv[i][1]);
                is_bytes = 0;
            } else {
                fprintf(stderr, "head: invalid option -- '%s'\n", argv[i]);
                return 1;
            }
        } else {
            if (file_count < 32) {
                files[file_count++] = argv[i];
            }
        }
    }

    if (file_count == 0) {
        return is_bytes ? head_stream_bytes(STDIN_FILENO, "(standard input)", max_bytes)
                        : head_stream_lines(STDIN_FILENO, "(standard input)", max_lines);
    }

    int had_error = 0;
    int show_headers = (file_count > 1);

    for (int i = 0; i < file_count; ++i) {
        if (show_headers) {
            if (i > 0) printf("\n");
            printf("==> %s <==\n", files[i]);
        }

        int res = 0;
        if (strcmp(files[i], "-") == 0) {
            res = is_bytes ? head_stream_bytes(STDIN_FILENO, "(standard input)", max_bytes)
                           : head_stream_lines(STDIN_FILENO, "(standard input)", max_lines);
        } else {
            res = head_file(files[i], max_lines, max_bytes, is_bytes);
        }

        if (res != 0) {
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
