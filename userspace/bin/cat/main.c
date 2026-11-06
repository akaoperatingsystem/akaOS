#include <akaos/fs.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUF_SIZE 4096

static int opt_number_all = 0;       /* -n, --number */
static int opt_number_nonblank = 0;  /* -b, --number-nonblank */
static int opt_show_ends = 0;        /* -E, -e, -A, --show-ends */
static int opt_show_tabs = 0;        /* -T, -t, -A, --show-tabs */
static int opt_show_nonprinting = 0; /* -v, -e, -t, -A, --show-nonprinting */
static int opt_squeeze_blank = 0;    /* -s, --squeeze-blank */

static long line_number = 1;
static int at_line_start = 1;
static int empty_line_count = 0;

static void print_help(void) {
    printf("Usage: cat [OPTION]... [FILE]...\n"
           "Concatenate FILE(s) to standard output.\n\n"
           "With no FILE, or when FILE is -, read standard input.\n\n"
           "  -A, --show-all           equivalent to -vET\n"
           "  -b, --number-nonblank    number nonempty output lines, overrides -n\n"
           "  -e                       equivalent to -vE\n"
           "  -E, --show-ends          display $ at end of each line\n"
           "  -n, --number             number all output lines\n"
           "  -s, --squeeze-blank      suppress repeated empty output lines\n"
           "  -t                       equivalent to -vT\n"
           "  -T, --show-tabs          display TAB characters as ^I\n"
           "  -u                       (ignored)\n"
           "  -v, --show-nonprinting   use ^ and M- notation, except for LFD and TAB\n"
           "      --help               display this help and exit\n"
           "      --version            output version information and exit\n");
}

static void print_version(void) {
    printf("cat (akaOS coreutils) 0.2.79\n");
}

static int write_str(int fd, const char *str) {
    if (!str) return 0;
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

static void print_error(const char *target, const char *msg) {
    write_str(STDERR_FILENO, "cat: ");
    write_str(STDERR_FILENO, target);
    write_str(STDERR_FILENO, ": ");
    write_str(STDERR_FILENO, msg);
    write_str(STDERR_FILENO, "\n");
}

static void put_char_transformed(unsigned char c) {
    if (c == '\t') {
        if (opt_show_tabs) {
            putchar('^');
            putchar('I');
        } else {
            putchar('\t');
        }
        return;
    }

    if (c == '\n') {
        if (opt_show_ends) {
            putchar('$');
        }
        putchar('\n');
        return;
    }

    if (opt_show_nonprinting) {
        if (c >= 128) {
            putchar('M');
            putchar('-');
            c -= 128;
        }
        if (c < 32) {
            putchar('^');
            putchar((char)(c + 64));
            return;
        }
        if (c == 127) {
            putchar('^');
            putchar('?');
            return;
        }
    }

    putchar((int)c);
}

/* Fast raw pass-through when no options modifying text or line numbering are set */
static int cat_stream_fast(int fd, const char *name) {
    char buffer[BUF_SIZE];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EISDIR) {
                print_error(name, "Is a directory");
            } else {
                print_error(name, "Read error");
            }
            return 1;
        }
        if (count == 0) break;
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t res = write(STDOUT_FILENO, buffer + written, (size_t)count - written);
            if (res <= 0) return 1;
            written += (size_t)res;
        }
    }
    return 0;
}

static int cat_stream_transformed(int fd, const char *name) {
    unsigned char buffer[BUF_SIZE];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EISDIR) {
                print_error(name, "Is a directory");
            } else {
                print_error(name, "Read error");
            }
            return 1;
        }
        if (count == 0) break;

        for (ssize_t i = 0; i < count; ++i) {
            unsigned char c = buffer[i];

            if (at_line_start) {
                if (c == '\n') {
                    empty_line_count++;
                    if (opt_squeeze_blank && empty_line_count > 1) {
                        /* Squeeze extra blank lines */
                        continue;
                    }
                    if (opt_number_all && !opt_number_nonblank) {
                        printf("%6ld\t", line_number++);
                    }
                    put_char_transformed(c);
                    continue;
                } else {
                    empty_line_count = 0;
                    if (opt_number_nonblank) {
                        printf("%6ld\t", line_number++);
                    } else if (opt_number_all) {
                        printf("%6ld\t", line_number++);
                    }
                    at_line_start = 0;
                }
            }

            if (c == '\n') {
                at_line_start = 1;
            }
            put_char_transformed(c);
        }
    }
    return 0;
}

static int cat_file(const char *path) {
    int has_transform = (opt_number_all || opt_number_nonblank || opt_show_ends ||
                         opt_show_tabs || opt_show_nonprinting || opt_squeeze_blank);

    struct akaos_stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EACCES) {
            print_error(path, "Permission denied");
        } else {
            print_error(path, "No such file or directory");
        }
        return 1;
    }

    if (st.type == AKAOS_DT_DIRECTORY) {
        print_error(path, "Is a directory");
        return 1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EISDIR) {
            print_error(path, "Is a directory");
        } else if (errno == ENOENT) {
            print_error(path, "No such file or directory");
        } else if (errno == EACCES) {
            print_error(path, "Permission denied");
        } else {
            print_error(path, "Error opening file");
        }
        return 1;
    }

    int res = has_transform ? cat_stream_transformed(fd, path) : cat_stream_fast(fd, path);
    close(fd);
    return res;
}

int main(int argc, char **argv) {
    int file_start = argc;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "--show-all") == 0) {
            opt_show_nonprinting = 1;
            opt_show_ends = 1;
            opt_show_tabs = 1;
            continue;
        }
        if (strcmp(arg, "--number-nonblank") == 0) {
            opt_number_nonblank = 1;
            continue;
        }
        if (strcmp(arg, "--show-ends") == 0) {
            opt_show_ends = 1;
            continue;
        }
        if (strcmp(arg, "--number") == 0) {
            opt_number_all = 1;
            continue;
        }
        if (strcmp(arg, "--squeeze-blank") == 0) {
            opt_squeeze_blank = 1;
            continue;
        }
        if (strcmp(arg, "--show-tabs") == 0) {
            opt_show_tabs = 1;
            continue;
        }
        if (strcmp(arg, "--show-nonprinting") == 0) {
            opt_show_nonprinting = 1;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            file_start = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                switch (arg[c]) {
                    case 'A':
                        opt_show_nonprinting = 1;
                        opt_show_ends = 1;
                        opt_show_tabs = 1;
                        break;
                    case 'b':
                        opt_number_nonblank = 1;
                        break;
                    case 'e':
                        opt_show_nonprinting = 1;
                        opt_show_ends = 1;
                        break;
                    case 'E':
                        opt_show_ends = 1;
                        break;
                    case 'n':
                        opt_number_all = 1;
                        break;
                    case 's':
                        opt_squeeze_blank = 1;
                        break;
                    case 't':
                        opt_show_nonprinting = 1;
                        opt_show_tabs = 1;
                        break;
                    case 'T':
                        opt_show_tabs = 1;
                        break;
                    case 'u':
                        /* ignored per POSIX / GNU */
                        break;
                    case 'v':
                        opt_show_nonprinting = 1;
                        break;
                    default:
                        fprintf(stderr, "cat: invalid option -- '%c'\n"
                                        "Try 'cat --help' for more information.\n", arg[c]);
                        return 1;
                }
            }
            continue;
        }
        file_start = i;
        break;
    }

    int has_transform = (opt_number_all || opt_number_nonblank || opt_show_ends ||
                         opt_show_tabs || opt_show_nonprinting || opt_squeeze_blank);

    if (file_start >= argc) {
        return has_transform ? cat_stream_transformed(STDIN_FILENO, "standard input")
                             : cat_stream_fast(STDIN_FILENO, "standard input");
    }

    int had_error = 0;
    for (int i = file_start; i < argc; ++i) {
        if (strcmp(argv[i], "-") == 0) {
            if (has_transform) {
                if (cat_stream_transformed(STDIN_FILENO, "standard input") != 0) had_error = 1;
            } else {
                if (cat_stream_fast(STDIN_FILENO, "standard input") != 0) had_error = 1;
            }
        } else {
            if (cat_file(argv[i]) != 0) {
                had_error = 1;
            }
        }
    }

    return had_error ? 1 : 0;
}
