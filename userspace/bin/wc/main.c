#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int flag_lines = 0;
static int flag_words = 0;
static int flag_bytes = 0;

static int is_whitespace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
}

static void print_counts(int lines, int words, int bytes, const char *name) {
    int first = 1;
    if (flag_lines) {
        printf("%s%d", first ? "" : " ", lines);
        first = 0;
    }
    if (flag_words) {
        printf("%s%d", first ? "" : " ", words);
        first = 0;
    }
    if (flag_bytes) {
        printf("%s%d", first ? "" : " ", bytes);
        first = 0;
    }
    if (name && name[0] != '\0') {
        printf(" %s", name);
    }
    printf("\n");
}

static int wc_fd(int fd, const char *name, int *out_l, int *out_w, int *out_b) {
    char buffer[512];
    int lines = 0;
    int words = 0;
    int bytes = 0;
    int in_word = 0;

    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            fprintf(stderr, "wc: %s: %s\n", name,
                    errno == EISDIR ? "Is a directory" : "Read error");
            return 1;
        }
        if (n == 0) break;

        bytes += (int)n;
        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                lines++;
            }
            if (is_whitespace(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    *out_l = lines;
    *out_w = words;
    *out_b = bytes;
    return 0;
}

static int wc_file(const char *path, int *out_l, int *out_w, int *out_b) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "wc: %s: %s\n", path,
                errno == ENOENT ? "No such file or directory" : "Cannot open file");
        return 1;
    }

    int res = wc_fd(fd, path, out_l, out_w, out_b);
    close(fd);
    return res;
}

int main(int argc, char **argv) {
    const char *files[32];
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            for (size_t j = 1; argv[i][j]; ++j) {
                if (argv[i][j] == 'l') flag_lines = 1;
                else if (argv[i][j] == 'w') flag_words = 1;
                else if (argv[i][j] == 'c') flag_bytes = 1;
                else {
                    fprintf(stderr, "wc: invalid option -- '%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (file_count < 32) {
                files[file_count++] = argv[i];
            }
        }
    }

    if (!flag_lines && !flag_words && !flag_bytes) {
        flag_lines = 1;
        flag_words = 1;
        flag_bytes = 1;
    }

    if (file_count == 0) {
        int l = 0, w = 0, b = 0;
        if (wc_fd(STDIN_FILENO, "(standard input)", &l, &w, &b) != 0) {
            return 1;
        }
        print_counts(l, w, b, "");
        return 0;
    }

    int total_lines = 0;
    int total_words = 0;
    int total_bytes = 0;
    int had_error = 0;

    for (int i = 0; i < file_count; ++i) {
        int l = 0, w = 0, b = 0;
        int res = 0;
        if (strcmp(files[i], "-") == 0) {
            res = wc_fd(STDIN_FILENO, "(standard input)", &l, &w, &b);
            if (res == 0) {
                print_counts(l, w, b, "-");
            }
        } else {
            res = wc_file(files[i], &l, &w, &b);
            if (res == 0) {
                print_counts(l, w, b, files[i]);
            }
        }

        if (res != 0) {
            had_error = 1;
        } else {
            total_lines += l;
            total_words += w;
            total_bytes += b;
        }
    }

    if (file_count > 1) {
        print_counts(total_lines, total_words, total_bytes, "total");
    }

    return had_error ? 1 : 0;
}
