#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERSION "8.32 (akaOS coreutils)"

static int suppress1 = 0; /* -1 */
static int suppress2 = 0; /* -2 */
static int suppress3 = 0; /* -3 */
static const char *delim = "\t";

static void print_version(void) {
    printf("comm %s\n", VERSION);
}

static void print_usage(void) {
    printf("Usage: comm [OPTION]... FILE1 FILE2\n"
           "Compare sorted files FILE1 and FILE2 line by line.\n\n"
           "When FILE1 or FILE2 (not both) is -, read standard input.\n\n"
           "  -1                      suppress column 1 (lines unique to FILE1)\n"
           "  -2                      suppress column 2 (lines unique to FILE2)\n"
           "  -3                      suppress column 3 (lines that appear in both files)\n"
           "      --check-order       check that the input is correctly sorted\n"
           "      --nocheck-order     do not check that the input is correctly sorted\n"
           "      --output-delimiter=STR  separate columns with STR\n"
           "      --help              display this help and exit\n"
           "      --version           output version information and exit\n");
}

/* Print line with proper delimiters based on column suppression */
static void output_line(int col, const char *line) {
    if (col == 1) {
        if (!suppress1) {
            printf("%s", line);
            if (!strchr(line, '\n')) putchar('\n');
        }
    } else if (col == 2) {
        if (!suppress2) {
            if (!suppress1) printf("%s", delim);
            printf("%s", line);
            if (!strchr(line, '\n')) putchar('\n');
        }
    } else if (col == 3) {
        if (!suppress3) {
            if (!suppress1) printf("%s", delim);
            if (!suppress2) printf("%s", delim);
            printf("%s", line);
            if (!strchr(line, '\n')) putchar('\n');
        }
    }
}

int main(int argc, char **argv) {
    const char *file1 = NULL;
    const char *file2 = NULL;

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
        if (strcmp(arg, "--check-order") == 0) {
            continue;
        }
        if (strcmp(arg, "--nocheck-order") == 0) {
            continue;
        }
        if (strncmp(arg, "--output-delimiter=", 19) == 0) {
            delim = arg + 19;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc; ++j) {
                if (!file1) file1 = argv[j];
                else if (!file2) file2 = argv[j];
            }
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-") != 0) {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == '1') suppress1 = 1;
                else if (arg[c] == '2') suppress2 = 1;
                else if (arg[c] == '3') suppress3 = 1;
                else {
                    fprintf(stderr, "comm: invalid option -- '%c'\n"
                                    "Try 'comm --help' for more information.\n", arg[c]);
                    return 1;
                }
            }
            continue;
        }

        if (!file1) file1 = arg;
        else if (!file2) file2 = arg;
    }

    if (!file1 || !file2) {
        fprintf(stderr, "comm: missing operand\n"
                        "Try 'comm --help' for more information.\n");
        return 1;
    }

    FILE *f1 = (strcmp(file1, "-") == 0) ? stdin : fopen(file1, "r");
    if (!f1) {
        fprintf(stderr, "comm: %s: %s\n", file1, strerror(errno));
        return 1;
    }

    FILE *f2 = (strcmp(file2, "-") == 0) ? stdin : fopen(file2, "r");
    if (!f2) {
        fprintf(stderr, "comm: %s: %s\n", file2, strerror(errno));
        if (f1 != stdin) fclose(f1);
        return 1;
    }

    char *line1 = NULL;
    size_t cap1 = 0;
    ssize_t len1 = 0;

    char *line2 = NULL;
    size_t cap2 = 0;
    ssize_t len2 = 0;

    len1 = getline(&line1, &cap1, f1);
    len2 = getline(&line2, &cap2, f2);

    while (len1 > 0 || len2 > 0) {
        if (len1 > 0 && len2 > 0) {
            int cmp = strcmp(line1, line2);
            if (cmp < 0) {
                output_line(1, line1);
                len1 = getline(&line1, &cap1, f1);
            } else if (cmp > 0) {
                output_line(2, line2);
                len2 = getline(&line2, &cap2, f2);
            } else {
                output_line(3, line1);
                len1 = getline(&line1, &cap1, f1);
                len2 = getline(&line2, &cap2, f2);
            }
        } else if (len1 > 0) {
            output_line(1, line1);
            len1 = getline(&line1, &cap1, f1);
        } else {
            output_line(2, line2);
            len2 = getline(&line2, &cap2, f2);
        }
    }

    if (line1) free(line1);
    if (line2) free(line2);
    if (f1 != stdin) fclose(f1);
    if (f2 != stdin) fclose(f2);

    return 0;
}
