#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int s_count_mode = 0;
static int s_dup_only = 0;
static int s_uniq_only = 0;
static int s_ignore_case = 0;

static inline char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int str_compare(const char *s1, const char *s2) {
    if (s_ignore_case) {
        while (*s1 && *s2) {
            char c1 = to_lower_char(*s1);
            char c2 = to_lower_char(*s2);
            if (c1 != c2) return c1 - c2;
            s1++;
            s2++;
        }
        return (unsigned char)*s1 - (unsigned char)*s2;
    }
    return strcmp(s1, s2);
}

static void print_usage(void) {
    printf("Usage: uniq [OPTIONS] [INPUT [OUTPUT]]\n");
    printf("Filter adjacent matching lines from INPUT (or standard input).\n\n");
    printf("Options:\n");
    printf("  -c           prefix lines by the number of occurrences\n");
    printf("  -d           only print duplicate lines, one for each group\n");
    printf("  -u           only print unique lines\n");
    printf("  -i           ignore differences in case when comparing\n");
    printf("  -h, --help   display this help and exit\n");
}

static void output_group(int out_fd, const char *line, int count) {
    if (count <= 0) return;
    if (s_dup_only && count == 1) return;
    if (s_uniq_only && count > 1) return;

    if (s_count_mode) {
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "%7d ", count);
        write(out_fd, prefix, strlen(prefix));
    }
    write(out_fd, line, strlen(line));
    write(out_fd, "\n", 1);
}

int main(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_file = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            for (size_t c = 1; argv[i][c]; ++c) {
                if (argv[i][c] == 'c') s_count_mode = 1;
                else if (argv[i][c] == 'd') s_dup_only = 1;
                else if (argv[i][c] == 'u') s_uniq_only = 1;
                else if (argv[i][c] == 'i') s_ignore_case = 1;
            }
        } else {
            if (!input_file) {
                input_file = argv[i];
            } else if (!output_file) {
                output_file = argv[i];
            }
        }
    }

    int in_fd = STDIN_FILENO;
    if (input_file && strcmp(input_file, "-") != 0) {
        in_fd = open(input_file, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "uniq: %s: No such file or directory\n", input_file);
            return 1;
        }
    }

    int out_fd = STDOUT_FILENO;
    if (output_file) {
        out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            fprintf(stderr, "uniq: cannot open %s\n", output_file);
            if (in_fd != STDIN_FILENO) close(in_fd);
            return 1;
        }
    }

    static char s_read_buf[4096];
    char current_line[1024] = "";
    char prev_line[1024] = "";
    int current_count = 0;
    size_t line_len = 0;
    ssize_t n;

    while ((n = read(in_fd, s_read_buf, sizeof(s_read_buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char ch = s_read_buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                current_line[line_len] = '\0';
                if (current_count == 0) {
                    strcpy(prev_line, current_line);
                    current_count = 1;
                } else if (str_compare(prev_line, current_line) == 0) {
                    current_count++;
                } else {
                    output_group(out_fd, prev_line, current_count);
                    strcpy(prev_line, current_line);
                    current_count = 1;
                }
                line_len = 0;
            } else {
                if (line_len + 1 < sizeof(current_line)) {
                    current_line[line_len++] = ch;
                }
            }
        }
    }

    if (line_len > 0) {
        current_line[line_len] = '\0';
        if (current_count == 0) {
            strcpy(prev_line, current_line);
            current_count = 1;
        } else if (str_compare(prev_line, current_line) == 0) {
            current_count++;
        } else {
            output_group(out_fd, prev_line, current_count);
            strcpy(prev_line, current_line);
            current_count = 1;
        }
    }

    if (current_count > 0) {
        output_group(out_fd, prev_line, current_count);
    }

    if (in_fd != STDIN_FILENO) close(in_fd);
    if (out_fd != STDOUT_FILENO) close(out_fd);
    return 0;
}
