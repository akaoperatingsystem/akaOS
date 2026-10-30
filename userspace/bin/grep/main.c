#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int flag_ignore_case = 0;
static int flag_line_number = 0;
static int flag_quiet = 0;
static int flag_invert = 0;

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static const char *find_pattern(const char *line, const char *pattern) {
    if (!line || !pattern) return NULL;
    if (*pattern == '\0') return line;

    size_t pat_len = strlen(pattern);
    size_t line_len = strlen(line);
    if (pat_len > line_len) return NULL;

    for (size_t i = 0; i <= line_len - pat_len; ++i) {
        size_t j = 0;
        while (j < pat_len) {
            char c1 = line[i + j];
            char c2 = pattern[j];
            if (flag_ignore_case) {
                c1 = to_lower(c1);
                c2 = to_lower(c2);
            }
            if (c1 != c2) break;
            ++j;
        }
        if (j == pat_len) {
            return line + i;
        }
    }
    return NULL;
}

static int grep_fd(int fd, const char *filename, const char *pattern, int show_filename) {
    char buffer[512];
    char line[1024];
    size_t line_idx = 0;
    int line_num = 1;
    int matches = 0;

    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (!flag_quiet) {
                fprintf(stderr, "grep: %s: read error\n", filename);
            }
            return -1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                line[line_idx] = '\0';
                int matched = (find_pattern(line, pattern) != NULL) ^ flag_invert;
                if (matched) {
                    matches++;
                    if (flag_quiet) {
                        return 1;
                    }
                    if (show_filename) {
                        printf("%s:", filename);
                    }
                    if (flag_line_number) {
                        printf("%d:", line_num);
                    }
                    printf("%s\n", line);
                }
                line_idx = 0;
                line_num++;
            } else {
                if (line_idx < sizeof(line) - 1) {
                    line[line_idx++] = c;
                }
            }
        }
    }

    if (line_idx > 0) {
        line[line_idx] = '\0';
        int matched = (find_pattern(line, pattern) != NULL) ^ flag_invert;
        if (matched) {
            matches++;
            if (flag_quiet) return 1;
            if (show_filename) {
                printf("%s:", filename);
            }
            if (flag_line_number) {
                printf("%d:", line_num);
            }
            printf("%s\n", line);
        }
    }

    return matches;
}

int main(int argc, char **argv) {
    const char *pattern = NULL;
    const char *files[32];
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            for (size_t j = 1; argv[i][j]; ++j) {
                if (argv[i][j] == 'i') flag_ignore_case = 1;
                else if (argv[i][j] == 'n') flag_line_number = 1;
                else if (argv[i][j] == 'q' || argv[i][j] == 's') flag_quiet = 1;
                else if (argv[i][j] == 'v') flag_invert = 1;
                else {
                    fprintf(stderr, "grep: invalid option -- '%c'\n", argv[i][j]);
                    return 2;
                }
            }
        } else if (!pattern) {
            pattern = argv[i];
        } else {
            if (file_count < 32) {
                files[file_count++] = argv[i];
            }
        }
    }

    if (!pattern) {
        fprintf(stderr, "Usage: grep [-inqsv] PATTERN [FILE...]\n");
        return 2;
    }

    int total_matches = 0;
    int had_error = 0;

    if (file_count == 0) {
        int m = grep_fd(STDIN_FILENO, "(standard input)", pattern, 0);
        if (m < 0) return 2;
        total_matches += m;
    } else {
        int show_name = (file_count > 1);
        for (int i = 0; i < file_count; ++i) {
            if (strcmp(files[i], "-") == 0) {
                int m = grep_fd(STDIN_FILENO, "(standard input)", pattern, show_name);
                if (m < 0) had_error = 1;
                else total_matches += m;
            } else {
                int fd = open(files[i], O_RDONLY);
                if (fd < 0) {
                    if (!flag_quiet) {
                        fprintf(stderr, "grep: %s: No such file or directory\n", files[i]);
                    }
                    had_error = 1;
                    continue;
                }
                int m = grep_fd(fd, files[i], pattern, show_name);
                close(fd);
                if (m < 0) had_error = 1;
                else {
                    total_matches += m;
                    if (flag_quiet && total_matches > 0) return 0;
                }
            }
        }
    }

    if (had_error && total_matches == 0) return 2;
    return total_matches > 0 ? 0 : 1;
}
