#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>

#define MAX_SELECT 1024
static uint8_t selected[MAX_SELECT];
static int select_all_above = 0;
static char line_buf[4096];
static char *output_delimiter = NULL;

static void print_usage(void) {
    printf("Usage: cut OPTION... [FILE]...\n");
    printf("Print selected parts of lines from each FILE to standard output.\n\n");
    printf("Options:\n");
    printf("  -d DELIM       use DELIM instead of TAB for field delimiter\n");
    printf("  -f LIST        select only these fields\n");
    printf("  -c LIST        select only these characters (bytes)\n");
    printf("  -b LIST        select only these bytes\n");
    printf("  -s             do not print lines not containing delimiters\n");
    printf("  --output-delimiter=STRING  use STRING as output delimiter\n");
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
    const char *sep = output_delimiter ? output_delimiter : (char[]){delim, '\0'};

    while (*cur) {
        const char *next = strchr(cur, delim);
        size_t flen = next ? (size_t)(next - cur) : strlen(cur);

        if (is_selected(field_idx)) {
            if (printed_any) printf("%s", sep);
            write(STDOUT_FILENO, cur, flen);
            printed_any = 1;
        }

        field_idx++;
        if (!next) break;
        cur = next + 1;
    }

    if (cur[0] == '\0' && is_selected(field_idx)) {
        if (printed_any) printf("%s", sep);
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
    int mode_fields = -1; /* 1 for fields, 0 for chars/bytes */
    int only_delim = 0;
    const char *list_str = NULL;
    int first_file = -1;
    int option_index = 0;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"only-delimited", no_argument, 0, 's'},
        {"output-delimiter", required_argument, 0, 1000},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:f:c:b:sh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                if (strlen(optarg) != 1) {
                    fprintf(stderr, "cut: the delimiter must be a single character\n");
                    return 1;
                }
                delim = optarg[0];
                break;
            case 'f': mode_fields = 1; list_str = optarg; break;
            case 'c':
            case 'b': mode_fields = 0; list_str = optarg; break;
            case 's': only_delim = 1; break;
            case 'h': print_usage(); return 0;
            case 1000: output_delimiter = optarg; break;
            default: print_usage(); return 1;
        }
    }
    first_file = optind;

    if (mode_fields == -1 || !list_str) {
        fprintf(stderr, "cut: you must specify a list of bytes, characters, or fields\n");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        return 1;
    }

    if (parse_range_list(list_str) != 0) {
        fprintf(stderr, "cut: invalid field or character list '%s'\n", list_str);
        return 1;
    }

    if (first_file >= argc) {
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
