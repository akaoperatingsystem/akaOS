#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_COMMANDS 32
#define LINE_MAX_LEN 2048

struct sed_cmd {
    int start_line; /* 0 if not specified, -1 if $ */
    int end_line;   /* 0 if not specified, -1 if $ */
    char addr_pat[64];
    char op;        /* 's', 'd', 'p', 'q' */
    char pattern[128];
    char replacement[128];
    int flag_g;
    int flag_p;
    int flag_i;
};

static struct sed_cmd commands[MAX_COMMANDS];
static int num_commands = 0;
static int quiet_mode = 0;
static int in_place = 0;

static char line_buffer[LINE_MAX_LEN];
static char pattern_space[LINE_MAX_LEN];
static char temp_space[LINE_MAX_LEN];

static int parse_int(const char **p) {
    int v = 0;
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v;
}

static int str_match_case(const char *haystack, const char *needle, int ignore_case) {
    if (!*needle) return 1;
    size_t nlen = strlen(needle);
    for (; *haystack; ++haystack) {
        if (!ignore_case) {
            if (strncmp(haystack, needle, nlen) == 0) return 1;
        } else {
            size_t match = 0;
            for (size_t k = 0; k < nlen; ++k) {
                char c1 = haystack[k];
                char c2 = needle[k];
                if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
                if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
                if (c1 == c2) match++;
                else break;
            }
            if (match == nlen) return 1;
        }
    }
    return 0;
}

static int parse_single_command(const char *str) {
    while (*str == ' ' || *str == '\t') str++;
    if (!*str || *str == '#') return 0;
    if (num_commands >= MAX_COMMANDS) return 0;

    struct sed_cmd *cmd = &commands[num_commands];
    memset(cmd, 0, sizeof(*cmd));

    /* Check address */
    if (*str == '$') {
        cmd->start_line = -1;
        str++;
    } else if (*str >= '0' && *str <= '9') {
        cmd->start_line = parse_int(&str);
        if (*str == ',') {
            str++;
            if (*str == '$') {
                cmd->end_line = -1;
                str++;
            } else if (*str >= '0' && *str <= '9') {
                cmd->end_line = parse_int(&str);
            }
        }
    } else if (*str == '/') {
        str++;
        size_t idx = 0;
        while (*str && *str != '/' && idx < sizeof(cmd->addr_pat) - 1) {
            cmd->addr_pat[idx++] = *str++;
        }
        cmd->addr_pat[idx] = '\0';
        if (*str == '/') str++;
    }

    while (*str == ' ' || *str == '\t') str++;
    if (!*str) return 0;

    cmd->op = *str++;
    if (cmd->op == 's') {
        char delim = *str++;
        if (!delim) return 0;
        size_t p_idx = 0;
        while (*str && *str != delim && p_idx < sizeof(cmd->pattern) - 1) {
            if (*str == '\\' && *(str + 1)) str++;
            cmd->pattern[p_idx++] = *str++;
        }
        cmd->pattern[p_idx] = '\0';
        if (*str == delim) str++;

        size_t r_idx = 0;
        while (*str && *str != delim && r_idx < sizeof(cmd->replacement) - 1) {
            if (*str == '\\' && *(str + 1)) str++;
            cmd->replacement[r_idx++] = *str++;
        }
        cmd->replacement[r_idx] = '\0';
        if (*str == delim) str++;

        while (*str && *str != ';' && *str != '\n') {
            if (*str == 'g') cmd->flag_g = 1;
            else if (*str == 'p') cmd->flag_p = 1;
            else if (*str == 'i' || *str == 'I') cmd->flag_i = 1;
            str++;
        }
    }

    num_commands++;
    return 1;
}

static void parse_script(const char *script) {
    char buf[256];
    size_t idx = 0;
    for (const char *p = script; *p; ++p) {
        if (*p == ';' || *p == '\n') {
            buf[idx] = '\0';
            parse_single_command(buf);
            idx = 0;
        } else if (idx < sizeof(buf) - 1) {
            buf[idx++] = *p;
        }
    }
    if (idx > 0) {
        buf[idx] = '\0';
        parse_single_command(buf);
    }
}

static void perform_substitute(struct sed_cmd *cmd, int *did_sub) {
    char *src = pattern_space;
    char *dst = temp_space;
    size_t pat_len = strlen(cmd->pattern);
    size_t rep_len = strlen(cmd->replacement);
    size_t dst_used = 0;
    int matches = 0;

    if (pat_len == 0) return;

    while (*src) {
        int match = 0;
        if (!cmd->flag_i) {
            match = (strncmp(src, cmd->pattern, pat_len) == 0);
        } else {
            match = 1;
            for (size_t k = 0; k < pat_len; ++k) {
                char c1 = src[k];
                char c2 = cmd->pattern[k];
                if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
                if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
                if (c1 != c2) { match = 0; break; }
            }
        }

        if (match && (cmd->flag_g || matches == 0)) {
            matches++;
            for (size_t r = 0; r < rep_len; ++r) {
                if (cmd->replacement[r] == '&') {
                    for (size_t m = 0; m < pat_len && dst_used + 1 < sizeof(temp_space); ++m) {
                        dst[dst_used++] = src[m];
                    }
                } else if (dst_used + 1 < sizeof(temp_space)) {
                    dst[dst_used++] = cmd->replacement[r];
                }
            }
            src += pat_len;
        } else {
            if (dst_used + 1 < sizeof(temp_space)) {
                dst[dst_used++] = *src++;
            } else {
                break;
            }
        }
    }
    dst[dst_used] = '\0';
    if (matches > 0) {
        strncpy(pattern_space, temp_space, sizeof(pattern_space) - 1);
        pattern_space[sizeof(pattern_space) - 1] = '\0';
        if (did_sub) *did_sub = 1;
    }
}

static void process_stream(int fd_in, int fd_out) {
    int line_no = 0;
    char in_byte = 0;
    size_t line_used = 0;
    int is_eof = 0;

    while (!is_eof) {
        line_used = 0;
        while (1) {
            ssize_t n = read(fd_in, &in_byte, 1);
            if (n <= 0) {
                is_eof = 1;
                break;
            }
            if (in_byte == '\n') break;
            if (line_used + 1 < sizeof(line_buffer)) {
                line_buffer[line_used++] = in_byte;
            }
        }
        if (is_eof && line_used == 0) break;
        line_buffer[line_used] = '\0';
        line_no++;

        strncpy(pattern_space, line_buffer, sizeof(pattern_space) - 1);
        pattern_space[sizeof(pattern_space) - 1] = '\0';

        int deleted = 0;
        int quit = 0;

        for (int c = 0; c < num_commands; ++c) {
            struct sed_cmd *cmd = &commands[c];
            int applies = 1;
            if (cmd->start_line > 0) {
                if (cmd->end_line > 0) {
                    if (line_no < cmd->start_line || line_no > cmd->end_line) applies = 0;
                } else {
                    if (line_no != cmd->start_line) applies = 0;
                }
            } else if (cmd->addr_pat[0]) {
                if (!str_match_case(pattern_space, cmd->addr_pat, 0)) applies = 0;
            }

            if (!applies) continue;

            if (cmd->op == 's') {
                int did_sub = 0;
                perform_substitute(cmd, &did_sub);
                if (did_sub && cmd->flag_p) {
                    write(fd_out, pattern_space, strlen(pattern_space));
                    write(fd_out, "\n", 1);
                }
            } else if (cmd->op == 'd') {
                deleted = 1;
                break;
            } else if (cmd->op == 'p') {
                write(fd_out, pattern_space, strlen(pattern_space));
                write(fd_out, "\n", 1);
            } else if (cmd->op == 'q') {
                quit = 1;
                break;
            }
        }

        if (!deleted && !quiet_mode) {
            write(fd_out, pattern_space, strlen(pattern_space));
            write(fd_out, "\n", 1);
        }

        if (quit) break;
    }
}

int main(int argc, char **argv) {
    const char *script = NULL;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-n") == 0) {
            quiet_mode = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-i") == 0) {
            in_place = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-e") == 0) {
            arg_idx++;
            if (arg_idx < argc) {
                parse_script(argv[arg_idx++]);
            }
        } else if (strcmp(argv[arg_idx], "--help") == 0 || strcmp(argv[arg_idx], "-h") == 0) {
            printf("Usage: sed [-n] [-i] [-e script] [script] [file...]\n"
                   "Stream editor for filtering and transforming text.\n\n"
                   "Options:\n"
                   "  -n            suppress automatic printing of pattern space\n"
                   "  -i            edit files in place\n"
                   "  -e script     add the script to the commands to be executed\n"
                   "  -h, --help    display this help and exit\n");
            return 0;
        } else {
            break;
        }
    }

    if (num_commands == 0 && arg_idx < argc) {
        script = argv[arg_idx++];
        parse_script(script);
    }

    if (num_commands == 0) {
        quiet_mode = 0;
    }

    if (arg_idx >= argc) {
        process_stream(STDIN_FILENO, STDOUT_FILENO);
        return 0;
    }

    for (int i = arg_idx; i < argc; ++i) {
        const char *filename = argv[i];
        if (strcmp(filename, "-") == 0) {
            process_stream(STDIN_FILENO, STDOUT_FILENO);
            continue;
        }

        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "sed: cannot open %s\n", filename);
            continue;
        }

        if (in_place) {
            char tmp_name[128];
            snprintf(tmp_name, sizeof(tmp_name), "%s.sedtmp", filename);
            int out_fd = open(tmp_name, O_WRONLY | O_CREAT | O_TRUNC);
            if (out_fd < 0) {
                fprintf(stderr, "sed: cannot create temp file for in-place edit\n");
                close(fd);
                continue;
            }
            process_stream(fd, out_fd);
            close(fd);
            close(out_fd);

            int rfd = open(tmp_name, O_RDONLY);
            int wfd = open(filename, O_WRONLY | O_TRUNC);
            if (rfd >= 0 && wfd >= 0) {
                char copy_buf[512];
                ssize_t n;
                while ((n = read(rfd, copy_buf, sizeof(copy_buf))) > 0) {
                    write(wfd, copy_buf, (size_t)n);
                }
            }
            if (rfd >= 0) close(rfd);
            if (wfd >= 0) close(wfd);
            unlink(tmp_name);
        } else {
            process_stream(fd, STDOUT_FILENO);
            close(fd);
        }
    }

    return 0;
}
