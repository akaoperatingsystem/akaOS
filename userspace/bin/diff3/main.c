#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DIFF3_VERSION "8.32 (akaOS coreutils)"
#define MAX_LINES 1024
#define MAX_LINE_LEN 1024

static char *lines1[MAX_LINES];
static char *lines2[MAX_LINES];
static char *lines3[MAX_LINES];
static int n1 = 0, n2 = 0, n3 = 0;

static int opt_text = 0;
static int opt_show_all = 0;
static int opt_ed = 0;
static int opt_overlap = 0;
static int opt_overlap_only = 0;
static int opt_easy_only = 0;
static int opt_merge = 0;
static int opt_append_wq = 0;
static int opt_initial_tab = 0;
static int opt_strip_cr = 0;

static const char *labels[3] = {NULL, NULL, NULL};
static int num_labels = 0;

static void print_version(void) {
    printf("diff3 %s\n", DIFF3_VERSION);
}

static void print_usage(void) {
    printf("Usage: diff3 [OPTION]... MYFILE OLDFILE YOURFILE\n"
           "Compare three files line by line.\n\n"
           "  -a, --text                 treat all files as text\n"
           "  -A, --show-all             output all changes, bracketing conflicts\n"
           "  -e, --ed                   output unmerged changes from OLDFILE to YOURFILE\n"
           "  -E, --show-overlap         output unmerged changes, bracketing conflicts\n"
           "  -m, --merge                output actual merged file, according to -A if no other -[AeExX3] option is given\n"
           "  -x, --overlap-only         output unmerged changes, output only overlapping changes\n"
           "  -X                         output only overlapping changes, bracketing them\n"
           "  -3, --easy-only            output only nonoverlapping changes\n"
           "  -i                         append 'w' and 'q' commands to ed scripts\n"
           "  -T, --initial-tab          output a tab rather than two spaces before line\n"
           "  -L, --label=LABEL          use LABEL instead of file name (can be repeated up to 3 times)\n"
           "      --strip-trailing-cr    strip trailing carriage return on input\n"
           "      --help                 display this help and exit\n"
           "  -v, --version              output version information and exit\n");
}

static int read_lines(const char *path, char *lines[MAX_LINES]) {
    int fd = (strcmp(path, "-") == 0) ? STDIN_FILENO : open(path, O_RDONLY);
    if (fd < 0) return -1;

    int count = 0;
    char buf[1];
    char line_buf[MAX_LINE_LEN];
    size_t col = 0;

    while (count < MAX_LINES) {
        ssize_t n = read(fd, buf, 1);
        if (n <= 0) break;
        if (buf[0] == '\r' && opt_strip_cr) continue;
        if (buf[0] == '\n') {
            line_buf[col] = '\0';
            lines[count] = malloc(col + 1);
            if (lines[count]) memcpy(lines[count], line_buf, col + 1);
            count++;
            col = 0;
        } else if (buf[0] != '\r') {
            if (col + 1 < MAX_LINE_LEN) {
                line_buf[col++] = buf[0];
            }
        }
    }
    if (col > 0 && count < MAX_LINES) {
        line_buf[col] = '\0';
        lines[count] = malloc(col + 1);
        if (lines[count]) memcpy(lines[count], line_buf, col + 1);
        count++;
    }

    if (fd != STDIN_FILENO) close(fd);
    return count;
}

static int lines_match(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 == s2;
    return strcmp(s1, s2) == 0;
}

int main(int argc, char **argv) {
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--text") == 0) {
            opt_text = 1;
            arg_idx++;
        } else if (strcmp(arg, "-A") == 0 || strcmp(arg, "--show-all") == 0) {
            opt_show_all = 1;
            arg_idx++;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--ed") == 0) {
            opt_ed = 1;
            arg_idx++;
        } else if (strcmp(arg, "-E") == 0 || strcmp(arg, "--show-overlap") == 0) {
            opt_overlap = 1;
            arg_idx++;
        } else if (strcmp(arg, "-x") == 0 || strcmp(arg, "--overlap-only") == 0) {
            opt_overlap_only = 1;
            arg_idx++;
        } else if (strcmp(arg, "-X") == 0) {
            opt_overlap_only = 1;
            opt_overlap = 1;
            arg_idx++;
        } else if (strcmp(arg, "-3") == 0 || strcmp(arg, "--easy-only") == 0) {
            opt_easy_only = 1;
            arg_idx++;
        } else if (strcmp(arg, "-m") == 0 || strcmp(arg, "--merge") == 0) {
            opt_merge = 1;
            arg_idx++;
        } else if (strcmp(arg, "-i") == 0) {
            opt_append_wq = 1;
            arg_idx++;
        } else if (strcmp(arg, "-T") == 0 || strcmp(arg, "--initial-tab") == 0) {
            opt_initial_tab = 1;
            arg_idx++;
        } else if (strcmp(arg, "--strip-trailing-cr") == 0) {
            opt_strip_cr = 1;
            arg_idx++;
        } else if (strcmp(arg, "-L") == 0 && arg_idx + 1 < argc) {
            if (num_labels < 3) labels[num_labels++] = argv[++arg_idx];
            arg_idx++;
        } else if (strncmp(arg, "--label=", 8) == 0) {
            if (num_labels < 3) labels[num_labels++] = arg + 8;
            arg_idx++;
        } else if (strcmp(arg, "--") == 0) {
            arg_idx++;
            break;
        } else {
            /* Combined short options e.g. -am, -mA */
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'a') opt_text = 1;
                else if (arg[c] == 'A') opt_show_all = 1;
                else if (arg[c] == 'e') opt_ed = 1;
                else if (arg[c] == 'E') opt_overlap = 1;
                else if (arg[c] == 'x') opt_overlap_only = 1;
                else if (arg[c] == 'X') { opt_overlap_only = 1; opt_overlap = 1; }
                else if (arg[c] == '3') opt_easy_only = 1;
                else if (arg[c] == 'm') opt_merge = 1;
                else if (arg[c] == 'i') opt_append_wq = 1;
                else if (arg[c] == 'T') opt_initial_tab = 1;
                else {
                    fprintf(stderr, "diff3: invalid option -- '%c'\n", arg[c]);
                    return 2;
                }
            }
            arg_idx++;
        }
    }

    if (argc - arg_idx < 3) {
        fprintf(stderr, "diff3: missing operand after '%s'\n", argv[argc - 1]);
        return 2;
    }

    const char *path1 = argv[arg_idx++];
    const char *path2 = argv[arg_idx++];
    const char *path3 = argv[arg_idx++];

    const char *lbl1 = (num_labels > 0 && labels[0]) ? labels[0] : path1;
    const char *lbl2 = (num_labels > 1 && labels[1]) ? labels[1] : path2;
    const char *lbl3 = (num_labels > 2 && labels[2]) ? labels[2] : path3;

    (void)lbl2;
    (void)opt_text;
    (void)opt_ed;
    (void)opt_overlap;
    (void)opt_overlap_only;
    (void)opt_easy_only;

    n1 = read_lines(path1, lines1);
    if (n1 < 0) {
        fprintf(stderr, "diff3: %s: %s\n", path1, strerror(errno));
        return 2;
    }
    n2 = read_lines(path2, lines2);
    if (n2 < 0) {
        fprintf(stderr, "diff3: %s: %s\n", path2, strerror(errno));
        return 2;
    }
    n3 = read_lines(path3, lines3);
    if (n3 < 0) {
        fprintf(stderr, "diff3: %s: %s\n", path3, strerror(errno));
        return 2;
    }

    /* Check if all 3 files are identical */
    int all_same = (n1 == n2 && n2 == n3);
    if (all_same) {
        for (int i = 0; i < n1; ++i) {
            if (!lines_match(lines1[i], lines2[i]) || !lines_match(lines1[i], lines3[i])) {
                all_same = 0;
                break;
            }
        }
    }

    if (all_same) {
        if (opt_merge) {
            for (int i = 0; i < n1; ++i) {
                printf("%s\n", lines1[i]);
            }
        }
        for (int i = 0; i < n1; ++i) free(lines1[i]);
        for (int i = 0; i < n2; ++i) free(lines2[i]);
        for (int i = 0; i < n3; ++i) free(lines3[i]);
        return 0;
    }

    int had_conflict = 0;

    if (opt_merge || opt_show_all) {
        /* Merge mode: emit merged output with conflict markers if needed */
        int i1 = 0, i2 = 0, i3 = 0;
        while (i1 < n1 || i2 < n2 || i3 < n3) {
            const char *l1 = (i1 < n1) ? lines1[i1] : NULL;
            const char *l2 = (i2 < n2) ? lines2[i2] : NULL;
            const char *l3 = (i3 < n3) ? lines3[i3] : NULL;

            if (lines_match(l1, l2) && lines_match(l1, l3)) {
                if (l1) printf("%s\n", l1);
                if (i1 < n1) i1++;
                if (i2 < n2) i2++;
                if (i3 < n3) i3++;
            } else if (lines_match(l1, l2)) {
                /* Changed only in YOURFILE (3) */
                if (l3) printf("%s\n", l3);
                if (i1 < n1) i1++;
                if (i2 < n2) i2++;
                if (i3 < n3) i3++;
            } else if (lines_match(l2, l3)) {
                /* Changed only in MYFILE (1) */
                if (l1) printf("%s\n", l1);
                if (i1 < n1) i1++;
                if (i2 < n2) i2++;
                if (i3 < n3) i3++;
            } else {
                /* Conflict between 1 and 3 against 2 */
                had_conflict = 1;
                printf("<<<<<<< %s\n", lbl1);
                if (l1) printf("%s\n", l1);
                printf("=======\n");
                if (l3) printf("%s\n", l3);
                printf(">>>>>>> %s\n", lbl3);
                if (i1 < n1) i1++;
                if (i2 < n2) i2++;
                if (i3 < n3) i3++;
            }
        }
    } else {
        /* Standard diff3 3-way report format */
        printf("====\n");
        printf("1:1,%dc\n", n1);
        for (int i = 0; i < n1; ++i) {
            printf("%s%s\n", opt_initial_tab ? "\t" : "  ", lines1[i]);
        }
        printf("2:1,%dc\n", n2);
        for (int i = 0; i < n2; ++i) {
            printf("%s%s\n", opt_initial_tab ? "\t" : "  ", lines2[i]);
        }
        printf("3:1,%dc\n", n3);
        for (int i = 0; i < n3; ++i) {
            printf("%s%s\n", opt_initial_tab ? "\t" : "  ", lines3[i]);
        }
        had_conflict = 1;
    }

    if (opt_append_wq) {
        printf("w\nq\n");
    }

    for (int i = 0; i < n1; ++i) free(lines1[i]);
    for (int i = 0; i < n2; ++i) free(lines2[i]);
    for (int i = 0; i < n3; ++i) free(lines3[i]);

    return had_conflict ? 1 : 0;
}
