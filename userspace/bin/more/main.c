#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

static void print_usage(void) {
    printf("Usage: more [OPTIONS] [FILE...]\n");
    printf("Standard file perusal filter for crt viewing.\n\n");
    printf("Options:\n");
    printf("  -h, --help   display this help and exit\n");
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
            break;
        }
    }

    int file_fd = STDIN_FILENO;
    int is_from_stdin = 1;
    if (filename && strcmp(filename, "-") != 0) {
        file_fd = open(filename, O_RDONLY);
        if (file_fd < 0) {
            fprintf(stderr, "more: %s: No such file or directory\n", filename);
            return 1;
        }
        is_from_stdin = 0;
    }

    if (is_from_stdin) {
        s_tty_in = open("/dev/tty", O_RDONLY);
        if (s_tty_in < 0) s_tty_in = open("/dev/tty1", O_RDONLY);
        if (s_tty_in < 0) s_tty_in = STDIN_FILENO;
    } else {
        s_tty_in = STDIN_FILENO;
    }

    struct winsize ws;
    int rows = 24, cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) rows = (int)ws.ws_row;
        if (ws.ws_col > 0) cols = (int)ws.ws_col;
    }

    static char s_read_buf[4096];
    static char s_line[1024];
    size_t line_len = 0;
    int displayed_lines = 0;
    int page_size = rows > 1 ? rows - 1 : 1;
    ssize_t n;
    int eof_reached = 0;

    enable_raw_mode();

    while (!eof_reached && (n = read(file_fd, s_read_buf, sizeof(s_read_buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            char ch = s_read_buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                if ((int)line_len > cols) line_len = cols;
                write(STDOUT_FILENO, s_line, line_len);
                write(STDOUT_FILENO, "\r\n", 2);
                line_len = 0;
                displayed_lines++;

                if (displayed_lines >= page_size) {
                    write(STDOUT_FILENO, "\033[7m--More--\033[0m", 15);
                    char cmd;
                    if (read(s_tty_in, &cmd, 1) <= 0 || cmd == 'q' || cmd == 'Q') {
                        write(STDOUT_FILENO, "\r          \r", 12);
                        eof_reached = 1;
                        break;
                    }
                    write(STDOUT_FILENO, "\r          \r", 12);
                    if (cmd == ' ' || cmd == 'f') {
                        displayed_lines = 0;
                    } else if (cmd == '\n' || cmd == '\r') {
                        displayed_lines = page_size - 1; // 1 more line
                    } else {
                        displayed_lines = 0;
                    }
                }
            } else {
                if (line_len + 1 < sizeof(s_line)) {
                    s_line[line_len++] = ch;
                }
            }
        }
    }

    if (!eof_reached && line_len > 0) {
        if ((int)line_len > cols) line_len = cols;
        write(STDOUT_FILENO, s_line, line_len);
        write(STDOUT_FILENO, "\r\n", 2);
    }

    disable_raw_mode();
    if (!is_from_stdin) close(file_fd);
    if (s_tty_in != STDIN_FILENO) close(s_tty_in);
    return 0;
}
