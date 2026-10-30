#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int s_numeric = 0;
static int s_reverse = 0;
static int s_unique = 0;
static int s_key_col = 0; // 0 = full line, 1-based column

static const char *get_column(const char *str, int col) {
    if (col <= 1) return str;
    int current = 1;
    while (*str) {
        while (*str == ' ' || *str == '\t') str++;
        if (current == col) return str;
        while (*str && *str != ' ' && *str != '\t') str++;
        current++;
    }
    return str;
}

static long long simple_atoll(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    long long val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

static int compare_lines(const void *a, const void *b) {
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    if (s_key_col > 0) {
        s1 = get_column(s1, s_key_col);
        s2 = get_column(s2, s_key_col);
    }
    int cmp;
    if (s_numeric) {
        long long n1 = simple_atoll(s1);
        long long n2 = simple_atoll(s2);
        if (n1 < n2) cmp = -1;
        else if (n1 > n2) cmp = 1;
        else cmp = 0;
    } else {
        cmp = strcmp(s1, s2);
    }
    return s_reverse ? -cmp : cmp;
}

static void quick_sort(char **arr, int low, int high, int (*cmp)(const void *, const void *)) {
    if (low < high) {
        char *pivot = arr[high];
        int i = low - 1;
        for (int j = low; j < high; ++j) {
            const void *p1 = &arr[j];
            const void *p2 = &pivot;
            if (cmp(p1, p2) <= 0) {
                i++;
                char *temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
        char *temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;
        int pi = i + 1;

        quick_sort(arr, low, pi - 1, cmp);
        quick_sort(arr, pi + 1, high, cmp);
    }
}

static void print_usage(void) {
    printf("Usage: sort [OPTIONS] [FILE...]\n");
    printf("Write sorted concatenation of all FILE(s) to standard output.\n\n");
    printf("Options:\n");
    printf("  -n           compare according to string numerical value\n");
    printf("  -r           reverse the result of comparisons\n");
    printf("  -u           output only the first of an equal run\n");
    printf("  -k POS       sort via a key starting at POS (1-based column)\n");
    printf("  -o FILE      write result to FILE instead of standard output\n");
    printf("  -h, --help   display this help and exit\n");
}

int main(int argc, char **argv) {
    const char *output_file = NULL;
    const char *input_files[16];
    int input_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            for (size_t c = 1; argv[i][c]; ++c) {
                if (argv[i][c] == 'n') s_numeric = 1;
                else if (argv[i][c] == 'r') s_reverse = 1;
                else if (argv[i][c] == 'u') s_unique = 1;
                else if (argv[i][c] == 'k' && i + 1 < argc) {
                    s_key_col = atoi(argv[++i]);
                    break;
                } else if (argv[i][c] == 'o' && i + 1 < argc) {
                    output_file = argv[++i];
                    break;
                }
            }
        } else {
            if (input_count < 16) {
                input_files[input_count++] = argv[i];
            }
        }
    }

    if (input_count == 0) {
        input_files[input_count++] = "-";
    }

    char **lines = NULL;
    int line_count = 0;
    int line_cap = 0;

    static char s_read_buf[4096];
    char line_buf[1024];
    size_t line_len = 0;

    for (int f = 0; f < input_count; ++f) {
        int fd = STDIN_FILENO;
        if (strcmp(input_files[f], "-") != 0) {
            fd = open(input_files[f], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "sort: cannot read '%s'\n", input_files[f]);
                continue;
            }
        }

        ssize_t n;
        while ((n = read(fd, s_read_buf, sizeof(s_read_buf))) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                char ch = s_read_buf[i];
                if (ch == '\r') continue;
                if (ch == '\n') {
                    if (line_count >= line_cap) {
                        int new_cap = line_cap ? line_cap * 2 : 64;
                        char **nl = malloc(sizeof(char *) * new_cap);
                        if (!nl) break;
                        for (int k = 0; k < line_count; ++k) nl[k] = lines[k];
                        free(lines);
                        lines = nl;
                        line_cap = new_cap;
                    }
                    char *dup = malloc(line_len + 1);
                    if (dup) {
                        memcpy(dup, line_buf, line_len);
                        dup[line_len] = '\0';
                        lines[line_count++] = dup;
                    }
                    line_len = 0;
                } else {
                    if (line_len + 1 < sizeof(line_buf)) {
                        line_buf[line_len++] = ch;
                    }
                }
            }
        }
        if (line_len > 0) {
            if (line_count >= line_cap) {
                int new_cap = line_cap ? line_cap * 2 : 64;
                char **nl = malloc(sizeof(char *) * new_cap);
                if (nl) {
                    for (int k = 0; k < line_count; ++k) nl[k] = lines[k];
                    free(lines);
                    lines = nl;
                    line_cap = new_cap;
                }
            }
            char *dup = malloc(line_len + 1);
            if (dup) {
                memcpy(dup, line_buf, line_len);
                dup[line_len] = '\0';
                lines[line_count++] = dup;
            }
            line_len = 0;
        }
        if (fd != STDIN_FILENO) close(fd);
    }

    if (line_count > 1 && lines) {
        quick_sort(lines, 0, line_count - 1, compare_lines);
    }

    int out_fd = STDOUT_FILENO;
    if (output_file) {
        out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            fprintf(stderr, "sort: open failed: %s\n", output_file);
            return 1;
        }
    }

    const char *last_printed = NULL;
    for (int i = 0; i < line_count; ++i) {
        if (s_unique && last_printed && strcmp(last_printed, lines[i]) == 0) {
            continue;
        }
        write(out_fd, lines[i], strlen(lines[i]));
        write(out_fd, "\n", 1);
        last_printed = lines[i];
    }

    if (output_file) close(out_fd);

    for (int i = 0; i < line_count; ++i) free(lines[i]);
    free(lines);
    return 0;
}
