#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DIFF_VERSION "8.32 (akaOS coreutils)"
#define MAX_LINES 1024
#define MAX_LINE_LEN 1024

static char *file1_lines[MAX_LINES];
static char *file2_lines[MAX_LINES];
static int nlines1 = 0;
static int nlines2 = 0;

/* Option flags */
static int opt_text = 0;                  /* -a, --text */
static int opt_ignore_blank_lines = 0;   /* -B, --ignore-blank-lines */
static int opt_ignore_space_change = 0;  /* -b, --ignore-space-change */
static int opt_context = 0;              /* -c, -C NUM, --context */
static int opt_unified = 1;              /* -u, -U NUM, --unified */
static int context_len = 3;
static int opt_ed = 0;                   /* -e, --ed */
static int opt_ignore_case = 0;          /* -i, --ignore-case */
static const char *opt_label1 = NULL;
static const char *opt_label2 = NULL;
static int opt_new_file = 0;             /* -N, --new-file */
static int opt_rcs = 0;                  /* -n, --rcs */
static int opt_brief = 0;                /* -q, --brief */
static int opt_recursive = 0;            /* -r, --recursive */
static int opt_report_identical = 0;     /* -s, --report-identical-files */
static int opt_strip_cr = 0;             /* --strip-trailing-cr */
static int opt_expand_tabs = 0;          /* -t, --expand-tabs */
static int opt_initial_tab = 0;          /* -T, --initial-tab */
static int opt_ignore_all_space = 0;     /* -w, --ignore-all-space */
static int opt_side_by_side = 0;         /* -y, --side-by-side */
static int opt_width = 130;              /* -W NUM, --width */
static int opt_suppress_common = 0;      /* --suppress-common-lines */
static int opt_left_column = 0;          /* --left-column */
static const char *opt_exclude = NULL;   /* -x PAT */

struct edit_op {
    char type; /* ' ', '-', '+' */
    int idx1;  /* 0-based in file1 */
    int idx2;  /* 0-based in file2 */
};
static struct edit_op *edit_script = NULL;
static int script_len = 0;

static void print_version(void) {
    printf("diff %s\n", DIFF_VERSION);
}

static void print_usage(void) {
    printf("Usage: diff [OPTION]... FILES\n"
           "Compare FILES line by line.\n\n"
           "  -a, --text                  treat all files as text\n"
           "  -b, --ignore-space-change   ignore changes in the amount of white space\n"
           "  -B, --ignore-blank-lines    ignore changes where lines are all blank\n"
           "  -c, -C NUM, --context[=NUM] output NUM (default 3) lines of copied context\n"
           "  -e, --ed                    output an ed script\n"
           "  -i, --ignore-case           ignore case differences in file contents\n"
           "  -n, --rcs                   output an RCS format diff\n"
           "  -N, --new-file              treat absent files as empty\n"
           "  -q, --brief                 report only when files differ\n"
           "  -r, --recursive             recursively compare any subdirectories found\n"
           "  -s, --report-identical-files report when two files are the same\n"
           "  -t, --expand-tabs           expand tabs to spaces in output\n"
           "  -T, --initial-tab           make tabs line up by prepending a tab\n"
           "  -u, -U NUM, --unified[=NUM] output NUM (default 3) lines of unified context\n"
           "  -w, --ignore-all-space      ignore all white space\n"
           "  -W, --width=NUM             output at most NUM (default 130) print columns\n"
           "  -y, --side-by-side          output in two columns\n"
           "      --left-column           output only the left column of common lines\n"
           "      --suppress-common-lines do not output common lines\n"
           "      --strip-trailing-cr     strip trailing carriage return on input\n"
           "      --help                  display this help and exit\n"
           "  -v, --version               output version information and exit\n");
}

static int read_file_lines(const char *path, char *lines[MAX_LINES]) {
    int fd = -1;
    if (strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            if (opt_new_file) return 0; /* Treat absent files as empty */
            return -1;
        }
    }

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

static int line_equal(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 == s2;
    if (!opt_ignore_case && !opt_ignore_all_space && !opt_ignore_space_change) {
        return strcmp(s1, s2) == 0;
    }

    char b1[MAX_LINE_LEN];
    char b2[MAX_LINE_LEN];
    size_t p1 = 0, p2 = 0;

    int in_ws = 0;
    for (size_t i = 0; s1[i] && p1 + 1 < MAX_LINE_LEN; ++i) {
        char c = s1[i];
        if (c >= 'A' && c <= 'Z' && opt_ignore_case) c = (char)(c + ('a' - 'A'));
        if (c == ' ' || c == '\t') {
            if (opt_ignore_all_space) continue;
            if (opt_ignore_space_change) {
                if (!in_ws) { b1[p1++] = ' '; in_ws = 1; }
                continue;
            }
        } else {
            in_ws = 0;
        }
        b1[p1++] = c;
    }
    if (opt_ignore_space_change && p1 > 0 && b1[p1 - 1] == ' ') p1--;
    b1[p1] = '\0';

    in_ws = 0;
    for (size_t i = 0; s2[i] && p2 + 1 < MAX_LINE_LEN; ++i) {
        char c = s2[i];
        if (c >= 'A' && c <= 'Z' && opt_ignore_case) c = (char)(c + ('a' - 'A'));
        if (c == ' ' || c == '\t') {
            if (opt_ignore_all_space) continue;
            if (opt_ignore_space_change) {
                if (!in_ws) { b2[p2++] = ' '; in_ws = 1; }
                continue;
            }
        } else {
            in_ws = 0;
        }
        b2[p2++] = c;
    }
    if (opt_ignore_space_change && p2 > 0 && b2[p2 - 1] == ' ') p2--;
    b2[p2] = '\0';

    return strcmp(b1, b2) == 0;
}

static void build_lcs(void) {
    if (edit_script) { free(edit_script); edit_script = NULL; script_len = 0; }
    int stride = nlines2 + 1;
    short *lcs = calloc((size_t)(nlines1 + 1) * (size_t)stride, sizeof(short));
    if (!lcs) return;

    for (int i = 1; i <= nlines1; ++i) {
        for (int j = 1; j <= nlines2; ++j) {
            if (line_equal(file1_lines[i - 1], file2_lines[j - 1])) {
                lcs[i * stride + j] = (short)(lcs[(i - 1) * stride + (j - 1)] + 1);
            } else if (lcs[(i - 1) * stride + j] >= lcs[i * stride + (j - 1)]) {
                lcs[i * stride + j] = lcs[(i - 1) * stride + j];
            } else {
                lcs[i * stride + j] = lcs[i * stride + (j - 1)];
            }
        }
    }

    int max_ops = nlines1 + nlines2 + 2;
    struct edit_op *rev = malloc((size_t)max_ops * sizeof(struct edit_op));
    if (!rev) { free(lcs); return; }

    int i = nlines1, j = nlines2;
    int k = 0;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && line_equal(file1_lines[i - 1], file2_lines[j - 1])) {
            rev[k].type = ' ';
            rev[k].idx1 = i - 1;
            rev[k].idx2 = j - 1;
            k++;
            i--;
            j--;
        } else if (j > 0 && (i == 0 || lcs[i * stride + (j - 1)] >= lcs[(i - 1) * stride + j])) {
            rev[k].type = '+';
            rev[k].idx1 = -1;
            rev[k].idx2 = j - 1;
            k++;
            j--;
        } else if (i > 0 && (j == 0 || lcs[i * stride + (j - 1)] < lcs[(i - 1) * stride + j])) {
            rev[k].type = '-';
            rev[k].idx1 = i - 1;
            rev[k].idx2 = -1;
            k++;
            i--;
        }
    }

    script_len = k;
    edit_script = malloc((size_t)(script_len + 1) * sizeof(struct edit_op));
    if (edit_script) {
        for (int idx = 0; idx < script_len; ++idx) {
            edit_script[idx] = rev[script_len - 1 - idx];
        }
    }
    free(rev);
    free(lcs);
}

/* Compare two individual files */
static int diff_files(const char *path1, const char *path2) {
    nlines1 = read_file_lines(path1, file1_lines);
    if (nlines1 < 0) {
        fprintf(stderr, "diff: %s: No such file or directory\n", path1);
        return 2;
    }
    nlines2 = read_file_lines(path2, file2_lines);
    if (nlines2 < 0) {
        fprintf(stderr, "diff: %s: No such file or directory\n", path2);
        return 2;
    }

    build_lcs();

    int has_diff = 0;
    for (int k = 0; k < script_len; ++k) {
        if (edit_script[k].type != ' ') {
            has_diff = 1;
            break;
        }
    }

    if (!has_diff) {
        if (opt_report_identical) {
            printf("Files %s and %s are identical\n", path1, path2);
        }
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 0;
    }

    if (opt_brief) {
        printf("Files %s and %s differ\n", path1, path2);
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 1;
    }

    const char *lbl1 = opt_label1 ? opt_label1 : path1;
    const char *lbl2 = opt_label2 ? opt_label2 : path2;

    /* 1. Side by side format */
    if (opt_side_by_side) {
        int half = (opt_width - 3) / 2;
        if (half < 10) half = 10;

        for (int k = 0; k < script_len; ++k) {
            char sep = ' ';
            const char *l1 = (edit_script[k].idx1 >= 0) ? file1_lines[edit_script[k].idx1] : "";
            const char *l2 = (edit_script[k].idx2 >= 0) ? file2_lines[edit_script[k].idx2] : "";

            if (edit_script[k].type == '-') { sep = '<'; }
            else if (edit_script[k].type == '+') { sep = '>'; }
            else {
                if (opt_suppress_common) continue;
                if (opt_left_column) {
                    printf("%s(\n", l1);
                    continue;
                }
            }

            printf("%-*.*s %c %s\n", half, half, l1, sep, l2);
        }
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 1;
    }

    /* 2. Context format */
    if (opt_context) {
        printf("*** %s\n", lbl1);
        printf("--- %s\n", lbl2);
        printf("***************\n");
        printf("*** 1,%d ****\n", nlines1);
        for (int k = 0; k < script_len; ++k) {
            if (edit_script[k].type == '-') {
                printf("- %s\n", file1_lines[edit_script[k].idx1]);
            } else if (edit_script[k].type == ' ') {
                printf("  %s\n", file1_lines[edit_script[k].idx1]);
            }
        }
        printf("--- 1,%d ----\n", nlines2);
        for (int k = 0; k < script_len; ++k) {
            if (edit_script[k].type == '+') {
                printf("+ %s\n", file2_lines[edit_script[k].idx2]);
            } else if (edit_script[k].type == ' ') {
                printf("  %s\n", file2_lines[edit_script[k].idx2]);
            }
        }
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 1;
    }

    /* 3. ed script format */
    if (opt_ed) {
        for (int k = script_len - 1; k >= 0; --k) {
            if (edit_script[k].type == '-') {
                printf("%dd\n", edit_script[k].idx1 + 1);
            } else if (edit_script[k].type == '+') {
                printf("%da\n%s\n.\n", edit_script[k].idx2, file2_lines[edit_script[k].idx2]);
            }
        }
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 1;
    }

    /* 4. RCS format */
    if (opt_rcs) {
        for (int k = 0; k < script_len; ++k) {
            if (edit_script[k].type == '-') {
                printf("d%d 1\n", edit_script[k].idx1 + 1);
            } else if (edit_script[k].type == '+') {
                printf("a%d 1\n%s\n", edit_script[k].idx2 + 1, file2_lines[edit_script[k].idx2]);
            }
        }
        for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
        for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
        return 1;
    }

    /* 5. Unified diff format (default) */
    printf("--- %s\n", lbl1);
    printf("+++ %s\n", lbl2);

    int idx = 0;
    while (idx < script_len) {
        while (idx < script_len && edit_script[idx].type == ' ') idx++;
        if (idx >= script_len) break;

        int start_idx = (idx > context_len) ? idx - context_len : 0;
        int end_idx = idx;

        while (end_idx < script_len) {
            if (edit_script[end_idx].type != ' ') {
                end_idx++;
            } else {
                int ctx_count = 0;
                int peek = end_idx;
                while (peek < script_len && edit_script[peek].type == ' ') {
                    ctx_count++;
                    peek++;
                }
                if (peek < script_len && ctx_count <= 2 * context_len) {
                    end_idx = peek;
                } else {
                    end_idx = (end_idx + context_len < script_len) ? end_idx + context_len : script_len;
                    break;
                }
            }
        }

        int f1_start = 0, f1_len = 0;
        int f2_start = 0, f2_len = 0;

        for (int k = start_idx; k < end_idx; ++k) {
            if (edit_script[k].type == ' ') {
                if (f1_start == 0) f1_start = edit_script[k].idx1 + 1;
                if (f2_start == 0) f2_start = edit_script[k].idx2 + 1;
                f1_len++;
                f2_len++;
            } else if (edit_script[k].type == '-') {
                if (f1_start == 0) f1_start = edit_script[k].idx1 + 1;
                f1_len++;
            } else if (edit_script[k].type == '+') {
                if (f2_start == 0) f2_start = edit_script[k].idx2 + 1;
                f2_len++;
            }
        }

        if (f1_start == 0) f1_start = 1;
        if (f2_start == 0) f2_start = 1;

        printf("@@ -%d,%d +%d,%d @@\n", f1_start, f1_len, f2_start, f2_len);

        for (int k = start_idx; k < end_idx; ++k) {
            if (opt_initial_tab) putchar('\t');
            if (edit_script[k].type == ' ') {
                printf(" %s\n", file1_lines[edit_script[k].idx1]);
            } else if (edit_script[k].type == '-') {
                printf("-%s\n", file1_lines[edit_script[k].idx1]);
            } else if (edit_script[k].type == '+') {
                printf("+%s\n", file2_lines[edit_script[k].idx2]);
            }
        }

        idx = end_idx;
    }

    for (int l = 0; l < nlines1; ++l) free(file1_lines[l]);
    for (int l = 0; l < nlines2; ++l) free(file2_lines[l]);
    return 1;
}

/* Recursive directory comparison */
static int diff_directories(const char *dir1, const char *dir2) {
    DIR *d1 = opendir(dir1);
    if (!d1) {
        fprintf(stderr, "diff: %s: %s\n", dir1, strerror(errno));
        return 2;
    }

    int had_diff = 0;
    struct dirent *ent1;

    while ((ent1 = readdir(d1)) != NULL) {
        if (strcmp(ent1->d_name, ".") == 0 || strcmp(ent1->d_name, "..") == 0) continue;
        if (opt_exclude && strstr(ent1->d_name, opt_exclude)) continue;

        char path1[512], path2[512];
        snprintf(path1, sizeof(path1), "%s/%s", dir1, ent1->d_name);
        snprintf(path2, sizeof(path2), "%s/%s", dir2, ent1->d_name);

        struct stat st1, st2;
        if (stat(path1, &st1) != 0) continue;
        int st2_res = stat(path2, &st2);

        if (st2_res != 0) {
            if (opt_new_file) {
                int r = diff_files(path1, "/dev/null");
                if (r != 0) had_diff = 1;
            } else {
                printf("Only in %s: %s\n", dir1, ent1->d_name);
                had_diff = 1;
            }
            continue;
        }

        if (S_ISDIR(st1.st_mode) && S_ISDIR(st2.st_mode)) {
            if (opt_recursive) {
                int r = diff_directories(path1, path2);
                if (r != 0) had_diff = 1;
            } else {
                printf("Common subdirectories: %s and %s\n", path1, path2);
            }
        } else {
            int r = diff_files(path1, path2);
            if (r != 0) had_diff = 1;
        }
    }
    closedir(d1);

    DIR *d2 = opendir(dir2);
    if (d2) {
        struct dirent *ent2;
        while ((ent2 = readdir(d2)) != NULL) {
            if (strcmp(ent2->d_name, ".") == 0 || strcmp(ent2->d_name, "..") == 0) continue;
            if (opt_exclude && strstr(ent2->d_name, opt_exclude)) continue;

            char path1[512], path2[512];
            snprintf(path1, sizeof(path1), "%s/%s", dir1, ent2->d_name);
            snprintf(path2, sizeof(path2), "%s/%s", dir2, ent2->d_name);

            struct stat st1;
            if (stat(path1, &st1) != 0) {
                if (opt_new_file) {
                    int r = diff_files("/dev/null", path2);
                    if (r != 0) had_diff = 1;
                } else {
                    printf("Only in %s: %s\n", dir2, ent2->d_name);
                    had_diff = 1;
                }
            }
        }
        closedir(d2);
    }

    return had_diff ? 1 : 0;
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
        } else if (strcmp(arg, "-b") == 0 || strcmp(arg, "--ignore-space-change") == 0) {
            opt_ignore_space_change = 1;
            arg_idx++;
        } else if (strcmp(arg, "-B") == 0 || strcmp(arg, "--ignore-blank-lines") == 0) {
            opt_ignore_blank_lines = 1;
            arg_idx++;
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--context") == 0) {
            opt_context = 1;
            opt_unified = 0;
            arg_idx++;
        } else if (strncmp(arg, "-C", 2) == 0) {
            opt_context = 1;
            opt_unified = 0;
            if (arg[2] != '\0') context_len = atoi(arg + 2);
            else if (arg_idx + 1 < argc) context_len = atoi(argv[++arg_idx]);
            arg_idx++;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--ed") == 0) {
            opt_ed = 1;
            opt_unified = 0;
            arg_idx++;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--ignore-case") == 0) {
            opt_ignore_case = 1;
            arg_idx++;
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--rcs") == 0) {
            opt_rcs = 1;
            opt_unified = 0;
            arg_idx++;
        } else if (strcmp(arg, "-N") == 0 || strcmp(arg, "--new-file") == 0) {
            opt_new_file = 1;
            arg_idx++;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--brief") == 0) {
            opt_brief = 1;
            arg_idx++;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--recursive") == 0) {
            opt_recursive = 1;
            arg_idx++;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--report-identical-files") == 0) {
            opt_report_identical = 1;
            arg_idx++;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--expand-tabs") == 0) {
            opt_expand_tabs = 1;
            arg_idx++;
        } else if (strcmp(arg, "-T") == 0 || strcmp(arg, "--initial-tab") == 0) {
            opt_initial_tab = 1;
            arg_idx++;
        } else if (strcmp(arg, "-u") == 0 || strcmp(arg, "--unified") == 0) {
            opt_unified = 1;
            opt_context = 0;
            arg_idx++;
        } else if (strncmp(arg, "-u", 2) == 0 && arg[2] >= '0' && arg[2] <= '9') {
            opt_unified = 1;
            opt_context = 0;
            context_len = atoi(arg + 2);
            arg_idx++;
        } else if (strncmp(arg, "-U", 2) == 0) {
            opt_unified = 1;
            opt_context = 0;
            if (arg[2] != '\0') context_len = atoi(arg + 2);
            else if (arg_idx + 1 < argc) context_len = atoi(argv[++arg_idx]);
            arg_idx++;
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--ignore-all-space") == 0) {
            opt_ignore_all_space = 1;
            arg_idx++;
        } else if (strcmp(arg, "-W") == 0 || strncmp(arg, "--width=", 8) == 0) {
            if (strncmp(arg, "--width=", 8) == 0) opt_width = atoi(arg + 8);
            else if (arg_idx + 1 < argc) opt_width = atoi(argv[++arg_idx]);
            arg_idx++;
        } else if (strcmp(arg, "-y") == 0 || strcmp(arg, "--side-by-side") == 0) {
            opt_side_by_side = 1;
            arg_idx++;
        } else if (strcmp(arg, "--strip-trailing-cr") == 0) {
            opt_strip_cr = 1;
            arg_idx++;
        } else if (strcmp(arg, "--left-column") == 0) {
            opt_left_column = 1;
            arg_idx++;
        } else if (strcmp(arg, "--suppress-common-lines") == 0) {
            opt_suppress_common = 1;
            arg_idx++;
        } else if (strcmp(arg, "--label") == 0 && arg_idx + 1 < argc) {
            if (!opt_label1) opt_label1 = argv[++arg_idx];
            else opt_label2 = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(arg, "-x") == 0 && arg_idx + 1 < argc) {
            opt_exclude = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(arg, "--") == 0) {
            arg_idx++;
            break;
        } else {
            /* Combined short options e.g. -ac, -ru, -iq */
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'a') opt_text = 1;
                else if (arg[c] == 'b') opt_ignore_space_change = 1;
                else if (arg[c] == 'B') opt_ignore_blank_lines = 1;
                else if (arg[c] == 'c') { opt_context = 1; opt_unified = 0; }
                else if (arg[c] == 'e') { opt_ed = 1; opt_unified = 0; }
                else if (arg[c] == 'i') opt_ignore_case = 1;
                else if (arg[c] == 'n') { opt_rcs = 1; opt_unified = 0; }
                else if (arg[c] == 'N') opt_new_file = 1;
                else if (arg[c] == 'q') opt_brief = 1;
                else if (arg[c] == 'r') opt_recursive = 1;
                else if (arg[c] == 's') opt_report_identical = 1;
                else if (arg[c] == 't') opt_expand_tabs = 1;
                else if (arg[c] == 'T') opt_initial_tab = 1;
                else if (arg[c] == 'u') { opt_unified = 1; opt_context = 0; }
                else if (arg[c] == 'w') opt_ignore_all_space = 1;
                else if (arg[c] == 'y') opt_side_by_side = 1;
                else {
                    fprintf(stderr, "diff: invalid option -- '%c'\n", arg[c]);
                    return 2;
                }
            }
            arg_idx++;
        }
    }

    if (argc - arg_idx < 2) {
        fprintf(stderr, "diff: missing operand after '%s'\n", argv[argc - 1]);
        return 2;
    }

    const char *path1 = argv[arg_idx++];
    const char *path2 = argv[arg_idx++];

    struct stat st1, st2;
    int st1_res = (strcmp(path1, "-") == 0) ? -1 : stat(path1, &st1);
    int st2_res = (strcmp(path2, "-") == 0) ? -1 : stat(path2, &st2);

    /* Directory vs Directory */
    if (st1_res == 0 && st2_res == 0 && S_ISDIR(st1.st_mode) && S_ISDIR(st2.st_mode)) {
        return diff_directories(path1, path2);
    }

    /* File vs Directory: e.g. diff DIR FILE */
    if (st1_res == 0 && S_ISDIR(st1.st_mode) && strcmp(path2, "-") != 0) {
        char full1[512];
        const char *base = strrchr(path2, '/');
        base = base ? base + 1 : path2;
        snprintf(full1, sizeof(full1), "%s/%s", path1, base);
        return diff_files(full1, path2);
    }

    /* Directory vs File: e.g. diff FILE DIR */
    if (st2_res == 0 && S_ISDIR(st2.st_mode) && strcmp(path1, "-") != 0) {
        char full2[512];
        const char *base = strrchr(path1, '/');
        base = base ? base + 1 : path1;
        snprintf(full2, sizeof(full2), "%s/%s", path2, base);
        return diff_files(path1, full2);
    }

    return diff_files(path1, path2);
}
