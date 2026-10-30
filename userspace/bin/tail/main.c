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
    fprintf(stderr, "tail: %s: %s\n", target, msg);
}

static char *my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

static int tail_stream_lines(int fd, const char *name, int max_lines) {
    if (max_lines <= 0) return 0;

    char **line_buf = calloc((size_t)max_lines, sizeof(char *));
    if (!line_buf) {
        print_error(name, "Memory allocation error");
        return 1;
    }

    int buf_start = 0;
    int line_count = 0;

    char buffer[512];
    char current_line[1024];
    size_t cur_len = 0;

    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            print_error(name, errno == EISDIR ? "Is a directory" : "Read error");
            for (int i = 0; i < max_lines; ++i) {
                if (line_buf[i]) free(line_buf[i]);
            }
            free(line_buf);
            return 1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                current_line[cur_len] = '\0';
                char *dup = my_strdup(current_line);
                if (dup) {
                    if (line_count < max_lines) {
                        line_buf[line_count++] = dup;
                    } else {
                        free(line_buf[buf_start]);
                        line_buf[buf_start] = dup;
                        buf_start = (buf_start + 1) % max_lines;
                    }
                }
                cur_len = 0;
            } else {
                if (cur_len < sizeof(current_line) - 1) {
                    current_line[cur_len++] = c;
                }
            }
        }
    }

    if (cur_len > 0) {
        current_line[cur_len] = '\0';
        char *dup = my_strdup(current_line);
        if (dup) {
            if (line_count < max_lines) {
                line_buf[line_count++] = dup;
            } else {
                free(line_buf[buf_start]);
                line_buf[buf_start] = dup;
                buf_start = (buf_start + 1) % max_lines;
            }
        }
    }

    for (int i = 0; i < line_count; ++i) {
        int idx = (buf_start + i) % max_lines;
        if (line_buf[idx]) {
            puts(line_buf[idx]);
            free(line_buf[idx]);
            line_buf[idx] = NULL;
        }
    }

    free(line_buf);
    return 0;
}

static int tail_file(const char *path, int max_lines) {
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

    int res = tail_stream_lines(fd, path, max_lines);
    close(fd);
    return res;
}

int main(int argc, char **argv) {
    int max_lines = 10;
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
                    fprintf(stderr, "tail: option requires an argument -- 'n'\n");
                    return 1;
                }
            } else if (argv[i][1] >= '0' && argv[i][1] <= '9') {
                max_lines = atoi(&argv[i][1]);
            } else {
                fprintf(stderr, "tail: invalid option -- '%s'\n", argv[i]);
                return 1;
            }
        } else {
            if (file_count < 32) {
                files[file_count++] = argv[i];
            }
        }
    }

    if (file_count == 0) {
        return tail_stream_lines(STDIN_FILENO, "(standard input)", max_lines);
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
            res = tail_stream_lines(STDIN_FILENO, "(standard input)", max_lines);
        } else {
            res = tail_file(files[i], max_lines);
        }

        if (res != 0) {
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
