#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int opt_list = 0;
static int opt_silent = 0;

int main(int argc, char **argv) {
    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "-l") == 0) {
            opt_list = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-s") == 0 || strcmp(argv[arg_idx], "--silent") == 0 || strcmp(argv[arg_idx], "--quiet") == 0) {
            opt_silent = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-h") == 0 || strcmp(argv[arg_idx], "--help") == 0) {
            printf("Usage: cmp [OPTION]... FILE1 [FILE2 [SKIP1 [SKIP2]]]\n"
                   "Compare two files byte by byte.\n\n"
                   "Options:\n"
                   "  -l            output byte numbers and differing byte values\n"
                   "  -s, --silent  suppress all normal output\n"
                   "  -h, --help    display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "cmp: invalid option -- '%s'\n", argv[arg_idx]);
            return 2;
        }
    }

    if (arg_idx >= argc) {
        fprintf(stderr, "cmp: missing operand after '%s'\n", argv[argc - 1]);
        return 2;
    }

    const char *path1 = argv[arg_idx++];
    const char *path2 = (arg_idx < argc) ? argv[arg_idx++] : "-";
    long skip1 = (arg_idx < argc) ? (long)atoi(argv[arg_idx++]) : 0;
    long skip2 = (arg_idx < argc) ? (long)atoi(argv[arg_idx++]) : 0;

    int fd1 = -1;
    int fd2 = -1;

    if (strcmp(path1, "-") == 0) {
        fd1 = STDIN_FILENO;
    } else {
        fd1 = open(path1, O_RDONLY);
        if (fd1 < 0) {
            if (!opt_silent) fprintf(stderr, "cmp: %s: No such file or directory\n", path1);
            return 2;
        }
    }

    if (strcmp(path2, "-") == 0) {
        fd2 = STDIN_FILENO;
    } else {
        fd2 = open(path2, O_RDONLY);
        if (fd2 < 0) {
            if (!opt_silent) fprintf(stderr, "cmp: %s: No such file or directory\n", path2);
            if (fd1 >= 0 && fd1 != STDIN_FILENO) close(fd1);
            return 2;
        }
    }

    /* Handle skips */
    char dummy;
    while (skip1 > 0 && read(fd1, &dummy, 1) > 0) skip1--;
    while (skip2 > 0 && read(fd2, &dummy, 1) > 0) skip2--;

    unsigned char b1[512];
    unsigned char b2[512];
    size_t byte_count = 0;
    size_t line_count = 1;
    int differ = 0;

    size_t pos1 = 0, len1 = 0;
    size_t pos2 = 0, len2 = 0;
    int eof1 = 0, eof2 = 0;

    while (1) {
        if (pos1 >= len1 && !eof1) {
            ssize_t n = read(fd1, b1, sizeof(b1));
            if (n <= 0) eof1 = 1;
            else { len1 = (size_t)n; pos1 = 0; }
        }
        if (pos2 >= len2 && !eof2) {
            ssize_t n = read(fd2, b2, sizeof(b2));
            if (n <= 0) eof2 = 1;
            else { len2 = (size_t)n; pos2 = 0; }
        }

        if (eof1 && eof2) break;

        if (eof1) {
            differ = 1;
            if (!opt_silent) {
                if (byte_count == 0) {
                    fprintf(stderr, "cmp: EOF on %s which is empty\n", path1);
                } else {
                    fprintf(stderr, "cmp: EOF on %s after byte %lu\n", path1, (unsigned long)byte_count);
                }
            }
            break;
        }
        if (eof2) {
            differ = 1;
            if (!opt_silent) {
                if (byte_count == 0) {
                    fprintf(stderr, "cmp: EOF on %s which is empty\n", path2);
                } else {
                    fprintf(stderr, "cmp: EOF on %s after byte %lu\n", path2, (unsigned long)byte_count);
                }
            }
            break;
        }

        unsigned char c1 = b1[pos1++];
        unsigned char c2 = b2[pos2++];
        byte_count++;

        if (c1 != c2) {
            differ = 1;
            if (opt_list) {
                printf("%6lu %3o %3o\n", (unsigned long)byte_count, (unsigned int)c1, (unsigned int)c2);
            } else if (!opt_silent) {
                printf("%s %s differ: char %lu, line %lu\n", path1, path2,
                       (unsigned long)byte_count, (unsigned long)line_count);
                break;
            }
        }

        if (c1 == '\n') line_count++;
    }

    if (fd1 >= 0 && fd1 != STDIN_FILENO) close(fd1);
    if (fd2 >= 0 && fd2 != STDIN_FILENO) close(fd2);

    return differ ? 1 : 0;
}
