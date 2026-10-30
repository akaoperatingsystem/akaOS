#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define MAX_LINES 1024
#define MAX_LINE_LEN 256
#define SCREEN_ROWS 22
#define SCREEN_COLS 80

struct editor_state {
    char *lines[MAX_LINES];
    int num_lines;
    int cursor_row;
    int cursor_col;
    int row_offset;
    int col_offset;
    char filename[128];
    int dirty;
    char status_msg[128];
};

static struct editor_state E;

static void write_str(int fd, const char *s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, s + written, len - written);
        if (res <= 0) break;
        written += (size_t)res;
    }
}

static void print_usage(void) {
    write_str(STDOUT_FILENO, "Usage: ted [OPTION]... [FILE]\n");
    write_str(STDOUT_FILENO, "Ted - Text Editor (ted).\n\n");
    write_str(STDOUT_FILENO, "  -h, --help     display this help and exit\n\n");
    write_str(STDOUT_FILENO, "Keybindings inside editor:\n");
    write_str(STDOUT_FILENO, "  Ctrl+O         Save current file to disk\n");
    write_str(STDOUT_FILENO, "  Ctrl+X         Exit editor\n");
    write_str(STDOUT_FILENO, "  Arrow keys     Navigate cursor\n");
}

static void editor_set_status(const char *msg) {
    size_t len = strlen(msg);
    if (len >= sizeof(E.status_msg)) len = sizeof(E.status_msg) - 1;
    for (size_t i = 0; i < len; ++i) E.status_msg[i] = msg[i];
    E.status_msg[len] = '\0';
}

static void editor_init(const char *filename) {
    E.num_lines = 0;
    E.cursor_row = 0;
    E.cursor_col = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.dirty = 0;
    E.status_msg[0] = '\0';

    if (filename) {
        size_t flen = strlen(filename);
        if (flen >= sizeof(E.filename)) flen = sizeof(E.filename) - 1;
        for (size_t i = 0; i < flen; ++i) E.filename[i] = filename[i];
        E.filename[flen] = '\0';
    } else {
        E.filename[0] = '\0';
    }
}

static void editor_insert_line(int at, const char *s, size_t len) {
    if (at < 0 || at > E.num_lines || E.num_lines >= MAX_LINES) return;
    for (int i = E.num_lines; i > at; --i) {
        E.lines[i] = E.lines[i - 1];
    }
    char *buf = malloc(len + 1);
    if (!buf) return;
    for (size_t i = 0; i < len; ++i) buf[i] = s[i];
    buf[len] = '\0';
    E.lines[at] = buf;
    ++E.num_lines;
    E.dirty = 1;
}

static void editor_free_lines(void) {
    for (int i = 0; i < E.num_lines; ++i) {
        if (E.lines[i]) {
            free(E.lines[i]);
            E.lines[i] = NULL;
        }
    }
    E.num_lines = 0;
}

static int editor_open(const char *filename) {
    editor_free_lines();
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        /* New file */
        editor_insert_line(0, "", 0);
        E.dirty = 0;
        editor_set_status("[ New File ]");
        return 1;
    }

    char chunk[512];
    char line_buf[MAX_LINE_LEN];
    size_t line_len = 0;
    ssize_t nread = 0;

    while ((nread = read(fd, chunk, sizeof(chunk))) > 0) {
        for (ssize_t i = 0; i < nread; ++i) {
            char c = chunk[i];
            if (c == '\n') {
                editor_insert_line(E.num_lines, line_buf, line_len);
                line_len = 0;
            } else if (c != '\r') {
                if (line_len < MAX_LINE_LEN - 1) {
                    line_buf[line_len++] = c;
                }
            }
        }
    }
    if (line_len > 0 || E.num_lines == 0) {
        editor_insert_line(E.num_lines, line_buf, line_len);
    }
    close(fd);
    E.dirty = 0;
    editor_set_status("[ File Loaded ]");
    return 1;
}

static int editor_save(void) {
    if (E.filename[0] == '\0') {
        editor_set_status("[ No filename specified ]");
        return 0;
    }

    int fd = open(E.filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        editor_set_status("[ Error saving file! ]");
        return 0;
    }

    for (int i = 0; i < E.num_lines; ++i) {
        size_t len = strlen(E.lines[i]);
        if (len > 0) {
            write(fd, E.lines[i], len);
        }
        write(fd, "\n", 1);
    }
    close(fd);
    sync();
    E.dirty = 0;
    editor_set_status("[ File Saved to Disk ]");
    return 1;
}

static void editor_render(void) {
    /* Clear screen and go home */
    write_str(STDOUT_FILENO, "\033[H\033[2J");

    /* Header bar (inverted) */
    write_str(STDOUT_FILENO, "\033[7m Ted 1.0  |  File: ");
    write_str(STDOUT_FILENO, E.filename[0] ? E.filename : "[No Name]");
    if (E.dirty) write_str(STDOUT_FILENO, " *");
    write_str(STDOUT_FILENO, "  |  [Ctrl+O: Save  Ctrl+X: Exit]\033[0m\n");

    /* Rows */
    for (int y = 0; y < SCREEN_ROWS; ++y) {
        int file_row = y + E.row_offset;
        if (file_row < E.num_lines) {
            const char *line = E.lines[file_row];
            size_t len = strlen(line);
            if (len > (size_t)E.col_offset) {
                const char *visible = line + E.col_offset;
                size_t vlen = strlen(visible);
                if (vlen > (size_t)SCREEN_COLS) vlen = (size_t)SCREEN_COLS;
                write(STDOUT_FILENO, visible, vlen);
            }
        } else {
            write_str(STDOUT_FILENO, "~");
        }
        write_str(STDOUT_FILENO, "\r\n");
    }

    /* Bottom status line (inverted) */
    write_str(STDOUT_FILENO, "\033[7m ");
    if (E.status_msg[0]) {
        write_str(STDOUT_FILENO, E.status_msg);
    } else {
        write_str(STDOUT_FILENO, "Line: 1, Col: 1");
    }
    write_str(STDOUT_FILENO, " \033[0m\r\n");
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage();
        return 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        write_str(STDERR_FILENO, "ted: invalid option or extra operand\n");
        return 1;
    }

    const char *target_file = (argc > 1) ? argv[1] : "untitled.txt";
    editor_init(target_file);
    editor_open(target_file);

    /* Render initial frame */
    editor_render();

    /* Process single batch/interactive loop */
    char c = 0;
    while (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == 0x18) { /* Ctrl+X: Exit */
            break;
        } else if (c == 0x0f) { /* Ctrl+O: Save */
            editor_save();
            editor_render();
        } else if (c == '\r' || c == '\n') {
            editor_insert_line(E.num_lines, "", 0);
            editor_render();
        } else if (c == '\033') {
            /* Escape sequence */
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                /* Arrow key or page */
                editor_render();
            }
        }
    }

    /* Clean exit: clear screen */
    write_str(STDOUT_FILENO, "\033[2J\033[H");
    editor_free_lines();
    return 0;
}
