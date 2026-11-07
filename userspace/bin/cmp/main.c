#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VERSION "3.12 (akaOS diffutils)"

static int opt_print_chars = 0;   /* -c, --print-chars */
static int opt_verbose = 0;       /* -l, --verbose */
static int opt_silent = 0;        /* -s, --silent, --quiet */
static long ignore_initial_1 = 0; /* --ignore-initial=BYTES */
static long ignore_initial_2 = 0;

static void print_version(void) {
    printf("cmp %s\n", VERSION);
}

static void print_usage(void) {
    printf("Usage: cmp [OPTION]... FROMFILE [TOFILE [SKIP1 [SKIP2]]]\n"
           "Compare two files byte by byte.\n\n"
           "  -c, --print-chars         print differing characters\n"
           "  -l, --verbose             output byte numbers and differing byte values\n"
           "  -s, --quiet, --silent     suppress all normal output\n"
           "      --ignore-initial=BYTES  skip first BYTES bytes of both inputs\n"
           "  -v, --version             output version information and exit\n"
           "      --help                display this help and exit\n\n"
           "An exit status of 0 means no differences were found, 1 means some differences\n"
           "were found, and 2 means trouble.\n");
}

/* Format character as control (^A), printable, or meta (M-^A) */
static void format_char(unsigned char c, char *buf, size_t sz) {
    if (c >= 128) {
        char sub[16];
        format_char(c - 128, sub, sizeof(sub));
        snprintf(buf, sz, "M-%s", sub);
        return;
    }
    if (c < 32) {
        snprintf(buf, sz, "^%c", (char)(c + '@'));
        return;
    }
    if (c == 127) {
        snprintf(buf, sz, "^?");
        return;
    }
    snprintf(buf, sz, "%c", (char)c);
}

static int open_file(const char *path, const char **disp_name) {
    if (!path || strcmp(path, "-") == 0) {
        if (disp_name) *disp_name = "-";
        return STDIN_FILENO;
    }
    if (disp_name) *disp_name = path;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (!opt_silent) {
            fprintf(stderr, "cmp: %s: %s\n", path, strerror(errno));
        }
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    const char *files[4];
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "--print-chars") == 0) {
            opt_print_chars = 1;
            continue;
        }
        if (strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
            continue;
        }
        if (strcmp(arg, "--silent") == 0 || strcmp(arg, "--quiet") == 0) {
            opt_silent = 1;
            continue;
        }
        if (strncmp(arg, "--ignore-initial=", 17) == 0) {
            char *val = arg + 17;
            char *colon = strchr(val, ':');
            if (colon) {
                *colon = '\0';
                ignore_initial_1 = atol(val);
                ignore_initial_2 = atol(colon + 1);
            } else {
                ignore_initial_1 = atol(val);
                ignore_initial_2 = ignore_initial_1;
            }
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc && file_count < 4; ++j) {
                files[file_count++] = argv[j];
            }
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-") != 0) {
            /* Support combined single-letter options like -cl, -sc, -v */
            for (size_t c = 1; arg[c]; ++c) {
                switch (arg[c]) {
                    case 'c':
                        opt_print_chars = 1;
                        break;
                    case 'l':
                        opt_verbose = 1;
                        break;
                    case 's':
                        opt_silent = 1;
                        break;
                    case 'v':
                        print_version();
                        return 0;
                    case 'i':
                        /* -i BYTES or -i BYTES1:BYTES2 */
                        if (arg[c + 1] != '\0') {
                            char *colon = strchr(arg + c + 1, ':');
                            if (colon) {
                                *colon = '\0';
                                ignore_initial_1 = atol(arg + c + 1);
                                ignore_initial_2 = atol(colon + 1);
                            } else {
                                ignore_initial_1 = atol(arg + c + 1);
                                ignore_initial_2 = ignore_initial_1;
                            }
                            c = strlen(arg) - 1;
                        } else if (i + 1 < argc) {
                            char *val = argv[++i];
                            char *colon = strchr(val, ':');
                            if (colon) {
                                *colon = '\0';
                                ignore_initial_1 = atol(val);
                                ignore_initial_2 = atol(colon + 1);
                            } else {
                                ignore_initial_1 = atol(val);
                                ignore_initial_2 = ignore_initial_1;
                            }
                        }
                        break;
                    default:
                        fprintf(stderr, "cmp: invalid option -- '%c'\n"
                                        "Try 'cmp --help' for more information.\n", arg[c]);
                        return 2;
                }
            }
            continue;
        }

        if (file_count < 4) {
            files[file_count++] = arg;
        }
    }

    if (file_count < 1) {
        fprintf(stderr, "cmp: missing operand after '%s'\n"
                        "Try 'cmp --help' for more information.\n", argv[argc - 1]);
        return 2;
    }

    const char *name1 = files[0];
    const char *name2 = (file_count >= 2) ? files[1] : "-";
    long skip1 = (file_count >= 3) ? atol(files[2]) : ignore_initial_1;
    long skip2 = (file_count >= 4) ? atol(files[3]) : ignore_initial_2;

    const char *disp1 = name1;
    const char *disp2 = name2;
    int fd1 = open_file(name1, &disp1);
    if (fd1 < 0) return 2;
    int fd2 = open_file(name2, &disp2);
    if (fd2 < 0) {
        if (fd1 >= 0 && fd1 != STDIN_FILENO) close(fd1);
        return 2;
    }

    /* Skip initial bytes */
    char dummy;
    while (skip1 > 0) {
        ssize_t n = read(fd1, &dummy, 1);
        if (n <= 0) break;
        skip1--;
    }
    while (skip2 > 0) {
        ssize_t n = read(fd2, &dummy, 1);
        if (n <= 0) break;
        skip2--;
    }

    unsigned char buf1[4096];
    unsigned char buf2[4096];
    size_t pos1 = 0, len1 = 0;
    size_t pos2 = 0, len2 = 0;
    int eof1 = 0, eof2 = 0;

    size_t byte_num = 0;
    size_t line_num = 1;
    int differences_found = 0;

    while (1) {
        if (pos1 >= len1 && !eof1) {
            ssize_t n = read(fd1, buf1, sizeof(buf1));
            if (n <= 0) eof1 = 1;
            else { len1 = (size_t)n; pos1 = 0; }
        }
        if (pos2 >= len2 && !eof2) {
            ssize_t n = read(fd2, buf2, sizeof(buf2));
            if (n <= 0) eof2 = 1;
            else { len2 = (size_t)n; pos2 = 0; }
        }

        if (eof1 && eof2) break;

        if (eof1) {
            differences_found = 1;
            if (!opt_silent) {
                if (byte_num == 0) {
                    fprintf(stderr, "cmp: EOF on %s which is empty\n", disp1);
                } else {
                    fprintf(stderr, "cmp: EOF on %s after byte %lu, line %lu\n", disp1,
                            (unsigned long)byte_num, (unsigned long)line_num);
                }
            }
            break;
        }
        if (eof2) {
            differences_found = 1;
            if (!opt_silent) {
                if (byte_num == 0) {
                    fprintf(stderr, "cmp: EOF on %s which is empty\n", disp2);
                } else {
                    fprintf(stderr, "cmp: EOF on %s after byte %lu, line %lu\n", disp2,
                            (unsigned long)byte_num, (unsigned long)line_num);
                }
            }
            break;
        }

        unsigned char b_val1 = buf1[pos1++];
        unsigned char b_val2 = buf2[pos2++];
        byte_num++;

        if (b_val1 != b_val2) {
            differences_found = 1;
            if (opt_verbose) {
                if (opt_print_chars) {
                    char c1_str[16], c2_str[16];
                    format_char(b_val1, c1_str, sizeof(c1_str));
                    format_char(b_val2, c2_str, sizeof(c2_str));
                    printf("%6lu %3o %-4s %3o %-4s\n",
                           (unsigned long)byte_num,
                           (unsigned int)b_val1, c1_str,
                           (unsigned int)b_val2, c2_str);
                } else {
                    printf("%6lu %3o %3o\n",
                           (unsigned long)byte_num,
                           (unsigned int)b_val1,
                           (unsigned int)b_val2);
                }
            } else if (!opt_silent) {
                if (opt_print_chars) {
                    char c1_str[16], c2_str[16];
                    format_char(b_val1, c1_str, sizeof(c1_str));
                    format_char(b_val2, c2_str, sizeof(c2_str));
                    printf("%s %s differ: byte %lu, line %lu is %3o %s %3o %s\n",
                           disp1, disp2,
                           (unsigned long)byte_num, (unsigned long)line_num,
                           (unsigned int)b_val1, c1_str,
                           (unsigned int)b_val2, c2_str);
                } else {
                    printf("%s %s differ: byte %lu, line %lu\n",
                           disp1, disp2,
                           (unsigned long)byte_num, (unsigned long)line_num);
                }
                break;
            }
        }

        if (b_val1 == '\n') {
            line_num++;
        }
    }

    if (fd1 >= 0 && fd1 != STDIN_FILENO) close(fd1);
    if (fd2 >= 0 && fd2 != STDIN_FILENO) close(fd2);

    return differences_found ? 1 : 0;
}
