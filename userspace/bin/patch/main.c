#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINES 512
#define MAX_LINE_LEN 512

static char *orig_lines[MAX_LINES];
static int num_orig_lines = 0;

static char *new_lines[MAX_LINES * 2];
static int num_new_lines = 0;

static int strip_count = 0;
static int opt_reverse = 0;
static int opt_silent = 0;
static const char *input_patch_file = NULL;
static const char *output_file = NULL;

static void append_new_line(const char *s) {
    if (num_new_lines < MAX_LINES * 2 && s) {
        size_t len = strlen(s);
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, s, len + 1);
            new_lines[num_new_lines++] = copy;
        }
    }
}

static const char *strip_path(const char *path, int strip) {
    while (strip > 0 && *path) {
        if (*path == '/') {
            strip--;
            path++;
        } else {
            path++;
        }
    }
    return path;
}

static int read_target_file(const char *path) {
    num_orig_lines = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[1];
    char line_buf[MAX_LINE_LEN];
    size_t col = 0;
    while (num_orig_lines < MAX_LINES) {
        ssize_t n = read(fd, buf, 1);
        if (n <= 0) break;
        if (buf[0] == '\n') {
            line_buf[col] = '\0';
            char *copy = malloc(col + 1);
            if (copy) memcpy(copy, line_buf, col + 1);
            orig_lines[num_orig_lines++] = copy;
            col = 0;
        } else if (buf[0] != '\r') {
            if (col + 1 < MAX_LINE_LEN) {
                line_buf[col++] = buf[0];
            }
        }
    }
    if (col > 0 && num_orig_lines < MAX_LINES) {
        line_buf[col] = '\0';
        char *copy = malloc(col + 1);
        if (copy) memcpy(copy, line_buf, col + 1);
        orig_lines[num_orig_lines++] = copy;
    }
    close(fd);
    return num_orig_lines;
}

static int write_target_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    for (int i = 0; i < num_new_lines; ++i) {
        if (new_lines[i]) {
            write(fd, new_lines[i], strlen(new_lines[i]));
        }
        write(fd, "\n", 1);
    }
    close(fd);
    return 0;
}

/* Patch line representation */
struct patch_line {
    char type; /* ' ', '-', '+' */
    char content[MAX_LINE_LEN];
};

static struct patch_line hunk_lines[64];
static int hunk_len = 0;

int main(int argc, char **argv) {
    const char *target_file = NULL;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-p") == 0 || strncmp(arg, "-p", 2) == 0) {
            if (arg[2] != '\0') {
                strip_count = atoi(arg + 2);
                arg_idx++;
            } else {
                arg_idx++;
                if (arg_idx < argc) strip_count = atoi(argv[arg_idx++]);
            }
        } else if (strcmp(arg, "-R") == 0 || strcmp(arg, "--reverse") == 0) {
            opt_reverse = 1;
            arg_idx++;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--silent") == 0 || strcmp(arg, "--quiet") == 0) {
            opt_silent = 1;
            arg_idx++;
        } else if (strcmp(arg, "-i") == 0) {
            arg_idx++;
            if (arg_idx < argc) input_patch_file = argv[arg_idx++];
        } else if (strcmp(arg, "-o") == 0) {
            arg_idx++;
            if (arg_idx < argc) output_file = argv[arg_idx++];
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: patch [options] [targetfile [patchfile]]\n"
                   "Apply a diff file to an original.\n\n"
                   "Options:\n"
                   "  -p <num>      strip <num> leading slashes from file names\n"
                   "  -R, --reverse assume patch was created with reversed files\n"
                   "  -i <patch>    read patch from file instead of stdin\n"
                   "  -o <outfile>  write output to specified file\n"
                   "  -s, --silent  work silently unless error occurs\n"
                   "  -h, --help    display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "patch: unrecognized option '%s'\n", arg);
            return 2;
        }
    }

    if (arg_idx < argc) target_file = argv[arg_idx++];
    if (arg_idx < argc && !input_patch_file) input_patch_file = argv[arg_idx++];

    int patch_fd = STDIN_FILENO;
    if (input_patch_file && strcmp(input_patch_file, "-") != 0) {
        patch_fd = open(input_patch_file, O_RDONLY);
        if (patch_fd < 0) {
            fprintf(stderr, "patch: cannot open %s\n", input_patch_file);
            return 2;
        }
    }

    char file_header_old[128] = "";
    char file_header_new[128] = "";
    char line_buf[MAX_LINE_LEN];

    int in_hunk = 0;
    int hunk_old_start = 0;
    int orig_cursor = 0;
    int read_target_done = 0;

    while (1) {
        /* Read line from patch */
        size_t col = 0;
        char ch;
        int n;
        while ((n = read(patch_fd, &ch, 1)) > 0) {
            if (ch == '\n') break;
            if (ch != '\r' && col + 1 < sizeof(line_buf)) {
                line_buf[col++] = ch;
            }
        }
        if (n <= 0 && col == 0) break;
        line_buf[col] = '\0';

        if (strncmp(line_buf, "--- ", 4) == 0) {
            char *p = line_buf + 4;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != '\t' && *end != ' ') end++;
            *end = '\0';
            strncpy(file_header_old, p, sizeof(file_header_old) - 1);
            continue;
        }
        if (strncmp(line_buf, "+++ ", 4) == 0) {
            char *p = line_buf + 4;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != '\t' && *end != ' ') end++;
            *end = '\0';
            strncpy(file_header_new, p, sizeof(file_header_new) - 1);
            continue;
        }

        if (strncmp(line_buf, "@@ ", 3) == 0) {
            /* If we were in a hunk, apply it */
            if (in_hunk && hunk_len > 0) {
                /* Copy unchanged lines up to hunk start */
                int target_pos = hunk_old_start - 1;
                while (orig_cursor < target_pos && orig_cursor < num_orig_lines) {
                    append_new_line(orig_lines[orig_cursor++]);
                }

                /* Apply hunk lines */
                for (int h = 0; h < hunk_len; ++h) {
                    char type = hunk_lines[h].type;
                    if (opt_reverse) {
                        if (type == '+') type = '-';
                        else if (type == '-') type = '+';
                    }

                    if (type == ' ') {
                        if (orig_cursor < num_orig_lines) {
                            append_new_line(orig_lines[orig_cursor++]);
                        }
                    } else if (type == '-') {
                        if (orig_cursor < num_orig_lines) orig_cursor++;
                    } else if (type == '+') {
                        append_new_line(hunk_lines[h].content);
                    }
                }
                hunk_len = 0;
            }

            /* Parse new hunk header @@ -old_start,old_len +new_start,new_len @@ */
            const char *hp = line_buf + 3;
            while (*hp == ' ' || *hp == '-') hp++;
            hunk_old_start = atoi(hp);
            in_hunk = 1;

            if (!read_target_done) {
                const char *actual_target = target_file;
                if (!actual_target) {
                    const char *cand = opt_reverse ? file_header_old : file_header_new;
                    actual_target = strip_path(cand, strip_count);
                }
                if (!actual_target || !*actual_target) {
                    fprintf(stderr, "patch: cannot determine file to patch\n");
                    if (patch_fd != STDIN_FILENO) close(patch_fd);
                    return 2;
                }
                if (!opt_silent) {
                    printf("patching file %s\n", actual_target);
                }
                if (read_target_file(actual_target) < 0) {
                    fprintf(stderr, "patch: %s: No such file or directory\n", actual_target);
                    if (patch_fd != STDIN_FILENO) close(patch_fd);
                    return 2;
                }
                read_target_done = 1;
            }
            continue;
        }

        if (in_hunk) {
            char prefix = line_buf[0];
            if (prefix == ' ' || prefix == '+' || prefix == '-') {
                if (hunk_len < (int)(sizeof(hunk_lines) / sizeof(hunk_lines[0]))) {
                    hunk_lines[hunk_len].type = prefix;
                    strncpy(hunk_lines[hunk_len].content, line_buf + 1, MAX_LINE_LEN - 1);
                    hunk_lines[hunk_len].content[MAX_LINE_LEN - 1] = '\0';
                    hunk_len++;
                }
            }
        }
    }

    if (in_hunk && hunk_len > 0) {
        int target_pos = hunk_old_start - 1;
        while (orig_cursor < target_pos && orig_cursor < num_orig_lines) {
            append_new_line(orig_lines[orig_cursor++]);
        }
        for (int h = 0; h < hunk_len; ++h) {
            char type = hunk_lines[h].type;
            if (opt_reverse) {
                if (type == '+') type = '-';
                else if (type == '-') type = '+';
            }
            if (type == ' ') {
                if (orig_cursor < num_orig_lines) {
                    append_new_line(orig_lines[orig_cursor++]);
                }
            } else if (type == '-') {
                if (orig_cursor < num_orig_lines) orig_cursor++;
            } else if (type == '+') {
                append_new_line(hunk_lines[h].content);
            }
        }
    }

    /* Copy remainder of original file */
    while (orig_cursor < num_orig_lines) {
        append_new_line(orig_lines[orig_cursor++]);
    }

    if (patch_fd != STDIN_FILENO) close(patch_fd);

    const char *out_path = output_file ? output_file : (target_file ? target_file : strip_path(opt_reverse ? file_header_old : file_header_new, strip_count));
    if (!out_path || !*out_path) return 1;

    if (write_target_file(out_path) < 0) {
        fprintf(stderr, "patch: failed to write %s\n", out_path);
        return 2;
    }

    return 0;
}
