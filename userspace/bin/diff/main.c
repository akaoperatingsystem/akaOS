#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINES 512
#define MAX_LINE_LEN 512

static char *file1_lines[MAX_LINES];
static char *file2_lines[MAX_LINES];
static int nlines1 = 0;
static int nlines2 = 0;

static int opt_unified = 1;
static int context_len = 3;
static int opt_ignore_case = 0;
static int opt_ignore_all_space = 0;
static int opt_ignore_space_change = 0;
static int opt_brief = 0;

/* Edit script entry */
struct edit_op {
    char type; /* ' ', '-', '+' */
    int idx1;  /* 0-based in file1 */
    int idx2;  /* 0-based in file2 */
};
static struct edit_op *edit_script = NULL;
static int script_len = 0;

static int read_file_lines(const char *path, char *lines[MAX_LINES]) {
    int fd = (strcmp(path, "-") == 0) ? STDIN_FILENO : open(path, O_RDONLY);
    if (fd < 0) return -1;

    int count = 0;
    char buf[1];
    char line_buf[MAX_LINE_LEN];
    size_t col = 0;

    while (count < MAX_LINES) {
        ssize_t n = read(fd, buf, 1);
        if (n <= 0) break;
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

    /* Normalize s1 */
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
    /* Trim trailing space for space-change */
    if (opt_ignore_space_change && p1 > 0 && b1[p1 - 1] == ' ') p1--;
    b1[p1] = '\0';

    /* Normalize s2 */
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

    /* Backtrack to construct edit script */
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

int main(int argc, char **argv) {
    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-u") == 0) {
            opt_unified = 1;
            arg_idx++;
        } else if (strncmp(arg, "-u", 2) == 0 && arg[2] >= '0' && arg[2] <= '9') {
            opt_unified = 1;
            context_len = atoi(arg + 2);
            arg_idx++;
        } else if (strcmp(arg, "-U") == 0) {
            opt_unified = 1;
            arg_idx++;
            if (arg_idx < argc) context_len = atoi(argv[arg_idx++]);
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--ignore-case") == 0) {
            opt_ignore_case = 1;
            arg_idx++;
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--ignore-all-space") == 0) {
            opt_ignore_all_space = 1;
            arg_idx++;
        } else if (strcmp(arg, "-b") == 0 || strcmp(arg, "--ignore-space-change") == 0) {
            opt_ignore_space_change = 1;
            arg_idx++;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--brief") == 0) {
            opt_brief = 1;
            arg_idx++;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: diff [OPTION]... FILES\n"
                   "Compare files line by line.\n\n"
                   "Options:\n"
                   "  -u, -U NUM    output NUM (default 3) lines of unified context\n"
                   "  -q, --brief   report only when files differ\n"
                   "  -i            ignore case differences in file contents\n"
                   "  -w            ignore all white space\n"
                   "  -b            ignore changes in the amount of white space\n"
                   "  -h, --help    display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "diff: unrecognized option '%s'\n", arg);
            return 2;
        }
    }

    if (argc - arg_idx < 2) {
        fprintf(stderr, "diff: missing operand after '%s'\n", argv[argc - 1]);
        return 2;
    }

    const char *path1 = argv[arg_idx++];
    const char *path2 = argv[arg_idx++];

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

    if (!has_diff) return 0;

    if (opt_brief) {
        printf("Files %s and %s differ\n", path1, path2);
        return 1;
    }

    printf("--- %s\n", path1);
    printf("+++ %s\n", path2);

    /* Print unified hunks */
    int idx = 0;
    while (idx < script_len) {
        /* Find next diff */
        while (idx < script_len && edit_script[idx].type == ' ') idx++;
        if (idx >= script_len) break;

        /* Determine hunk bounds */
        int start_idx = (idx > context_len) ? idx - context_len : 0;
        int end_idx = idx;

        while (end_idx < script_len) {
            if (edit_script[end_idx].type != ' ') {
                end_idx++;
            } else {
                /* Check how many context lines follow */
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

        /* Calculate line ranges for hunk header */
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

    return 1;
}
