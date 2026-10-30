#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static struct termios s_orig_termios;
static int s_raw_mode_active = 0;
static int s_tty_in = STDIN_FILENO;

static void disable_raw_mode(void) {
    if (s_raw_mode_active) {
        tcsetattr(s_tty_in, TCSAFLUSH, &s_orig_termios);
        s_raw_mode_active = 0;
    }
}

static void enable_raw_mode(void) {
    if (!isatty(s_tty_in)) return;
    if (tcgetattr(s_tty_in, &s_orig_termios) == 0) {
        struct termios raw = s_orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(s_tty_in, TCSAFLUSH, &raw);
        s_raw_mode_active = 1;
    }
}

static char **s_lines = NULL;
static int s_line_count = 0;
static int s_line_cap = 0;

static int add_line(const char *buf, size_t len) {
    if (s_line_count >= s_line_cap) {
        int new_cap = s_line_cap ? s_line_cap * 2 : 64;
        char **new_lines = malloc(sizeof(char *) * new_cap);
        if (!new_lines) return 0;
        for (int i = 0; i < s_line_count; ++i) {
            new_lines[i] = s_lines[i];
        }
        free(s_lines);
        s_lines = new_lines;
        s_line_cap = new_cap;
    }
    char *dup = malloc(len + 1);
    if (!dup) return 0;
    memcpy(dup, buf, len);
    dup[len] = '\0';
    s_lines[s_line_count++] = dup;
    return 1;
}

static void free_lines(void) {
    for (int i = 0; i < s_line_count; ++i) {
        free(s_lines[i]);
    }
    free(s_lines);
    s_lines = NULL;
    s_line_count = 0;
    s_line_cap = 0;
}

static int load_file(int fd) {
    static char s_read_buf[4096];
    char line_buf[1024];
    size_t line_len = 0;
    ssize_t n;

    while ((n = read(fd, s_read_buf, sizeof(s_read_buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char ch = s_read_buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                add_line(line_buf, line_len);
                line_len = 0;
            } else {
                if (line_len + 1 < sizeof(line_buf)) {
                    line_buf[line_len++] = ch;
                }
            }
        }
    }
    if (line_len > 0) {
        add_line(line_buf, line_len);
    }
    return s_line_count;
}

static void print_usage(void) {
    printf("Usage: less [OPTIONS] [FILE]\n");
    printf("Interactive text file pager.\n\n");
    printf("Options:\n");
    printf("  -h, --help   display this help and exit\n\n");
    printf("Commands inside pager:\n");
    printf("  Space, f, PgDn   Scroll forward one screen\n");
    printf("  b, PgUp          Scroll backward one screen\n");
    printf("  Enter, j, Down   Scroll forward one line\n");
    printf("  k, y, Up         Scroll backward one line\n");
    printf("  g, <             Go to first line of file\n");
    printf("  G, >             Go to last line of file\n");
    printf("  /pattern         Search forward for pattern\n");
    printf("  n                Repeat previous search\n");
    printf("  q, Q             Exit less\n");
}

static void get_window_size(int *rows, int *cols) {
    struct winsize ws;
    *rows = 24;
    *cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) *rows = (int)ws.ws_row;
        if (ws.ws_col > 0) *cols = (int)ws.ws_col;
    }
}

static void render(int top, int rows, int cols, const char *status) {
    write(STDOUT_FILENO, "\033[H\033[J", 6);
    int page = rows > 1 ? rows - 1 : 1;
    for (int i = 0; i < page; ++i) {
        int idx = top + i;
        if (idx < s_line_count) {
            const char *l = s_lines[idx];
            size_t len = strlen(l);
            if ((int)len > cols) len = (size_t)cols;
            write(STDOUT_FILENO, l, len);
            write(STDOUT_FILENO, "\r\n", 2);
        } else {
            write(STDOUT_FILENO, "~\r\n", 3);
        }
    }
    if (status && *status) {
        write(STDOUT_FILENO, "\033[7m", 4);
        write(STDOUT_FILENO, status, strlen(status));
        write(STDOUT_FILENO, "\033[0m", 4);
    } else {
        write(STDOUT_FILENO, ":", 1);
    }
}

int main(int argc, char **argv) {
    const char *filename = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    int file_fd = STDIN_FILENO;
    int is_from_stdin = 1;
    if (filename && strcmp(filename, "-") != 0) {
        file_fd = open(filename, O_RDONLY);
        if (file_fd < 0) {
            fprintf(stderr, "less: %s: No such file or directory\n", filename);
            return 1;
        }
        is_from_stdin = 0;
    }

    load_file(file_fd);
    if (!is_from_stdin) {
        close(file_fd);
    }

    if (is_from_stdin) {
        s_tty_in = open("/dev/tty", O_RDONLY);
        if (s_tty_in < 0) s_tty_in = open("/dev/tty1", O_RDONLY);
        if (s_tty_in < 0) s_tty_in = STDIN_FILENO;
    } else {
        s_tty_in = STDIN_FILENO;
    }

    if (s_line_count == 0) {
        add_line("", 0);
    }

    enable_raw_mode();

    int rows, cols;
    get_window_size(&rows, &cols);
    int top = 0;
    char search_pattern[64] = "";
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "%s (lines 1-%d/%d)",
             filename ? filename : "stdin",
             rows - 1 < s_line_count ? rows - 1 : s_line_count,
             s_line_count);

    render(top, rows, cols, status_buf);

    int running = 1;
    while (running) {
        char ch;
        if (read(s_tty_in, &ch, 1) <= 0) break;

        int page = rows > 1 ? rows - 1 : 1;
        int max_top = s_line_count > page ? s_line_count - page : 0;
        status_buf[0] = '\0';

        if (ch == 'q' || ch == 'Q') {
            running = 0;
            break;
        } else if (ch == ' ' || ch == 'f') {
            top += page;
            if (top > max_top) top = max_top;
        } else if (ch == 'b') {
            top -= page;
            if (top < 0) top = 0;
        } else if (ch == '\n' || ch == '\r' || ch == 'j') {
            if (top < max_top) top++;
        } else if (ch == 'k' || ch == 'y') {
            if (top > 0) top--;
        } else if (ch == 'g' || ch == '<') {
            top = 0;
        } else if (ch == 'G' || ch == '>') {
            top = max_top;
        } else if (ch == '/') {
            disable_raw_mode();
            write(STDOUT_FILENO, "\r/", 2);
            size_t pidx = 0;
            while (pidx + 1 < sizeof(search_pattern)) {
                char c = 0;
                if (read(s_tty_in, &c, 1) <= 0 || c == '\n' || c == '\r') break;
                if (c == 0x7f || c == '\b') {
                    if (pidx > 0) {
                        pidx--;
                        write(STDOUT_FILENO, "\b \b", 3);
                    }
                } else {
                    search_pattern[pidx++] = c;
                    write(STDOUT_FILENO, &c, 1);
                }
            }
            search_pattern[pidx] = '\0';
            enable_raw_mode();
            if (pidx > 0) {
                int found = -1;
                for (int i = top + 1; i < s_line_count; ++i) {
                    if (strstr(s_lines[i], search_pattern)) {
                        found = i;
                        break;
                    }
                }
                if (found >= 0) {
                    top = found;
                    if (top > max_top) top = max_top;
                } else {
                    snprintf(status_buf, sizeof(status_buf), "Pattern not found: %s", search_pattern);
                }
            }
        } else if (ch == 'n') {
            if (search_pattern[0]) {
                int found = -1;
                for (int i = top + 1; i < s_line_count; ++i) {
                    if (strstr(s_lines[i], search_pattern)) {
                        found = i;
                        break;
                    }
                }
                if (found >= 0) {
                    top = found;
                    if (top > max_top) top = max_top;
                } else {
                    snprintf(status_buf, sizeof(status_buf), "Pattern not found: %s", search_pattern);
                }
            }
        } else if (ch == '\033') {
            char seq[4];
            if (read(s_tty_in, &seq[0], 1) > 0 && seq[0] == '[') {
                if (read(s_tty_in, &seq[1], 1) > 0) {
                    if (seq[1] == 'A') { // Up
                        if (top > 0) top--;
                    } else if (seq[1] == 'B') { // Down
                        if (top < max_top) top++;
                    } else if (seq[1] == '5' || seq[1] == '6') {
                        char t;
                        (void)read(s_tty_in, &t, 1); // skip ~
                        if (seq[1] == '5') { // PgUp
                            top -= page;
                            if (top < 0) top = 0;
                        } else { // PgDn
                            top += page;
                            if (top > max_top) top = max_top;
                        }
                    }
                }
            }
        }

        get_window_size(&rows, &cols);
        render(top, rows, cols, status_buf);
    }

    disable_raw_mode();
    write(STDOUT_FILENO, "\r\n", 2);
    free_lines();
    if (s_tty_in != STDIN_FILENO) close(s_tty_in);
    return 0;
}
