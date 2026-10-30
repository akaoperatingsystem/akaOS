#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_SELECT 1024
static uint8_t selected[MAX_SELECT];
static int select_all_above = 0;
static char line_buf[4096];

static void print_usage(void) {
    printf("Usage: cut OPTION... [FILE]...\n");
    printf("Print selected parts of lines from each FILE to standard output.\n\n");
    printf("Options:\n");
    printf("  -d DELIM       use DELIM instead of TAB for field delimiter\n");
    printf("  -f LIST        select only these fields\n");
    printf("  -c LIST        select only these characters (bytes)\n");
    printf("  -s             do not print lines not containing delimiters\n");
    printf("  -h, --help     display this help and exit\n");
}

static int parse_range_list(const char *list) {
    memset(selected, 0, sizeof(selected));
    select_all_above = 0;
    if (!list || !*list) return -1;

    const char *p = list;
    while (*p) {
        int start = 0, end = 0;
        int has_start = 0, has_end = 0;
        int is_range = 0;

        while (*p >= '0' && *p <= '9') {
            start = start * 10 + (*p - '0');
            has_start = 1;
            p++;
        }

        if (*p == '-') {
            is_range = 1;
            p++;
            while (*p >= '0' && *p <= '9') {
                end = end * 10 + (*p - '0');
                has_end = 1;
                p++;
            }
        }

        if (!is_range) {
            if (!has_start || start <= 0) return -1;
            if (start < MAX_SELECT) selected[start] = 1;
        } else {
            if (!has_start && !has_end) return -1;
            if (!has_start) start = 1;
            if (!has_end) {
                if (start < MAX_SELECT) {
                    if (select_all_above == 0 || start < select_all_above) {
                        select_all_above = start;
                    }
                }
            } else {
                if (start > end || start <= 0) return -1;
                for (int i = start; i <= end; i++) {
                    if (i < MAX_SELECT) selected[i] = 1;
                }
            }
        }

        if (*p == ',' || *p == ' ') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
    }
    return 0;
}

static int is_selected(int idx) {
    if (idx <= 0) return 0;
    if (select_all_above > 0 && idx >= select_all_above) return 1;
    if (idx < MAX_SELECT && selected[idx]) return 1;
    return 0;
}

static void process_line_fields(const char *line, char delim, int only_delim) {
    const char *d = strchr(line, delim);
    if (!d) {
        if (!only_delim) {
            printf("%s\n", line);
        }
        return;
    }

    int field_idx = 1;
    int printed_any = 0;
    const char *cur = line;

    while (*cur) {
        const char *next = strchr(cur, delim);
        size_t flen = next ? (size_t)(next - cur) : strlen(cur);

        if (is_selected(field_idx)) {
            if (printed_any) putchar(delim);
            write(STDOUT_FILENO, cur, flen);
            printed_any = 1;
        }

        field_idx++;
        if (!next) break;
        cur = next + 1;
    }

    if (cur[0] == '\0' && is_selected(field_idx)) {
        /* Trailing empty field */
        if (printed_any) putchar(delim);
    }

    putchar('\n');
}

static void process_line_chars(const char *line) {
    size_t len = strlen(line);
    for (size_t i = 0; i < len; i++) {
        int char_idx = (int)(i + 1);
        if (is_selected(char_idx)) {
            putchar(line[i]);
        }
    }
    putchar('\n');
}

static void process_fd(int fd, int mode_fields, char delim, int only_delim) {
    char buf[1024];
    ssize_t n;
    size_t line_len = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                if (mode_fields) {
                    process_line_fields(line_buf, delim, only_delim);
                } else {
                    process_line_chars(line_buf);
                }
                line_len = 0;
            } else if (c != '\r') {
                if (line_len < sizeof(line_buf) - 1) {
                    line_buf[line_len++] = c;
                }
            }
        }
    }
    if (line_len > 0) {
        line_buf[line_len] = '\0';
        if (mode_fields) {
            process_line_fields(line_buf, delim, only_delim);
        } else {
            process_line_chars(line_buf);
        }
    }
}

int main(int argc, char *argv[]) {
    char delim = '\t';
    int mode_fields = -1; /* 1 for fields, 0 for chars */
    int only_delim = 0;
    const char *list_str = NULL;
    int first_file = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--only-delimited") == 0) {
            only_delim = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cut: option requires an argument -- 'd'\n");
                return 1;
            }
            if (strlen(argv[i + 1]) != 1) {
                fprintf(stderr, "cut: the delimiter must be a single character\n");
                return 1;
            }
            delim = argv[i + 1][0];
            i++;
        } else if (strncmp(argv[i], "-d", 2) == 0 && strlen(argv[i]) == 3) {
            delim = argv[i][2];
        } else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cut: option requires an argument -- 'f'\n");
                return 1;
            }
            mode_fields = 1;
            list_str = argv[i + 1];
            i++;
        } else if (strncmp(argv[i], "-f", 2) == 0) {
            mode_fields = 1;
            list_str = argv[i] + 2;
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cut: option requires an argument -- 'c'\n");
                return 1;
            }
            mode_fields = 0;
            list_str = argv[i + 1];
            i++;
        } else if (strncmp(argv[i], "-c", 2) == 0) {
            mode_fields = 0;
            list_str = argv[i] + 2;
        } else if (strcmp(argv[i], "--") == 0) {
            first_file = i + 1;
            break;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "cut: invalid option -- '%s'\n", argv[i]);
            print_usage();
            return 1;
        } else {
            first_file = i;
            break;
        }
    }

    if (mode_fields == -1 || !list_str) {
        fprintf(stderr, "cut: you must specify a list of bytes, characters, or fields\n");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        return 1;
    }

    if (parse_range_list(list_str) != 0) {
        fprintf(stderr, "cut: invalid field or character list '%s'\n", list_str);
        return 1;
    }

    if (first_file == -1 || first_file >= argc) {
        process_fd(STDIN_FILENO, mode_fields, delim, only_delim);
        return 0;
    }

    int exit_code = 0;
    for (int i = first_file; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            process_fd(STDIN_FILENO, mode_fields, delim, only_delim);
        } else {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cut: %s: %s\n", argv[i], strerror(errno));
                exit_code = 1;
                continue;
            }
            process_fd(fd, mode_fields, delim, only_delim);
            close(fd);
        }
    }

    return exit_code;
}
