#include <akaos/display.h>
#include <akaos/version.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TERM_COLS 64
#define TERM_ROWS 20
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16
#define TERM_WIDTH (TERM_COLS * CHAR_WIDTH)
#define TERM_HEIGHT (TERM_ROWS * CHAR_HEIGHT)

/* Compact 8x16 ASCII glyph generator / fallback */
static void draw_glyph(uint32_t *pixels, uint32_t stride, int cx, int cy,
                       char ch, uint32_t fg, uint32_t bg) {
    /* 8x16 procedural rasterizer for standard ASCII printable characters */
    for (int y = 0; y < CHAR_HEIGHT; ++y) {
        for (int x = 0; x < CHAR_WIDTH; ++x) {
            int px = cx * CHAR_WIDTH + x;
            int py = cy * CHAR_HEIGHT + y;
            int pixel_on = 0;

            if (ch > 32 && ch < 127) {
                /* Render character shapes */
                if (ch >= 'A' && ch <= 'Z') {
                    if (x == 1 || x == 6 || (y == 2 && x > 1 && x < 6) ||
                        (y == 8 && x > 1 && x < 6 && ch != 'C' && ch != 'O' && ch != 'U')) {
                        pixel_on = 1;
                    }
                    if (ch == 'I' || ch == 'T') {
                        pixel_on = (y == 2) || (x == 4);
                    }
                    if (ch == 'L') {
                        pixel_on = (x == 2) || (y == 13 && x >= 2 && x <= 6);
                    }
                } else if (ch >= 'a' && ch <= 'z') {
                    if (x == 2 || (y == 7 && x >= 2 && x <= 5) || (y == 13 && x >= 2 && x <= 5) || (x == 5 && y >= 7 && y <= 13)) {
                        pixel_on = 1;
                    }
                } else if (ch >= '0' && ch <= '9') {
                    if (x == 1 || x == 6 || y == 2 || y == 13 || (ch == '0' && x == y / 2 + 1)) {
                        pixel_on = 1;
                    }
                } else if (ch == '$' || ch == '#' || ch == '@') {
                    if (x == 4 || y == 7 || y == 11) pixel_on = 1;
                } else if (ch == '/' || ch == '\\') {
                    if (ch == '/' && (x + y / 2 == 8)) pixel_on = 1;
                    if (ch == '\\' && (x == y / 2)) pixel_on = 1;
                } else if (ch == '-' || ch == '_') {
                    if (y == (ch == '-' ? 8 : 14)) pixel_on = 1;
                } else if (ch == ':' || ch == '.') {
                    if (x == 4 && (y == 13 || (ch == ':' && y == 8))) pixel_on = 1;
                } else {
                    if (x > 1 && x < 6 && y > 3 && y < 12) pixel_on = 1;
                }
            }

            pixels[py * stride + px] = pixel_on ? fg : bg;
        }
    }
}

struct terminal_state {
    char grid[TERM_ROWS][TERM_COLS];
    int cursor_x;
    int cursor_y;
    int master_fd;
    pid_t shell_pid;
    int shm_fd;
    size_t shm_size;
    char shm_name[32];
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void term_clear(struct terminal_state *term) {
    for (int r = 0; r < TERM_ROWS; ++r) {
        for (int c = 0; c < TERM_COLS; ++c) {
            term->grid[r][c] = ' ';
        }
    }
    term->cursor_x = 0;
    term->cursor_y = 0;
}

static void term_scroll(struct terminal_state *term) {
    for (int r = 0; r < TERM_ROWS - 1; ++r) {
        memcpy(term->grid[r], term->grid[r + 1], TERM_COLS);
    }
    for (int c = 0; c < TERM_COLS; ++c) {
        term->grid[TERM_ROWS - 1][c] = ' ';
    }
    term->cursor_y = TERM_ROWS - 1;
}

static void term_write_char(struct terminal_state *term, char ch) {
    if (ch == '\r') {
        term->cursor_x = 0;
        return;
    }
    if (ch == '\n') {
        term->cursor_x = 0;
        term->cursor_y++;
        if (term->cursor_y >= TERM_ROWS) {
            term_scroll(term);
        }
        return;
    }
    if (ch == '\b') {
        if (term->cursor_x > 0) {
            term->cursor_x--;
            term->grid[term->cursor_y][term->cursor_x] = ' ';
        }
        return;
    }
    if (ch == '\t') {
        int spaces = 4 - (term->cursor_x % 4);
        for (int i = 0; i < spaces; ++i) {
            term_write_char(term, ' ');
        }
        return;
    }
    if (ch >= 32 && ch < 127) {
        if (term->cursor_x >= TERM_COLS) {
            term->cursor_x = 0;
            term->cursor_y++;
            if (term->cursor_y >= TERM_ROWS) {
                term_scroll(term);
            }
        }
        term->grid[term->cursor_y][term->cursor_x++] = ch;
    }
}

static void term_render(struct terminal_state *term) {
    uint32_t bg = UINT32_C(0x181a1f);
    uint32_t fg = UINT32_C(0xabb2bf);
    uint32_t cursor_col = UINT32_C(0x528bff);

    for (int r = 0; r < TERM_ROWS; ++r) {
        for (int c = 0; c < TERM_COLS; ++c) {
            draw_glyph(term->pixels, TERM_WIDTH, c, r, term->grid[r][c], fg, bg);
        }
    }

    /* Draw cursor block */
    if (term->cursor_y >= 0 && term->cursor_y < TERM_ROWS &&
        term->cursor_x >= 0 && term->cursor_x < TERM_COLS) {
        int cx = term->cursor_x * CHAR_WIDTH;
        int cy = term->cursor_y * CHAR_HEIGHT;
        for (int y = 0; y < CHAR_HEIGHT; ++y) {
            for (int x = 0; x < CHAR_WIDTH; ++x) {
                if (y >= CHAR_HEIGHT - 3) {
                    term->pixels[(cy + y) * TERM_WIDTH + (cx + x)] = cursor_col;
                }
            }
        }
    }
}

static int start_terminal(struct terminal_state *term) {
    term_clear(term);
    pid_t my_pid = getpid();
    snprintf(term->shm_name, sizeof(term->shm_name), "/terminal-surf-%d", (int)my_pid);

    size_t content_size = sizeof(struct akaos_surface_header) +
                          (size_t)TERM_WIDTH * (size_t)TERM_HEIGHT * sizeof(uint32_t);
    term->shm_size = (content_size + 4095U) & ~(size_t)4095U;

    term->shm_fd = shm_open(term->shm_name, O_CREAT | O_RDWR, 0666);
    if (term->shm_fd < 0 && errno == EEXIST) {
        shm_unlink(term->shm_name);
        term->shm_fd = shm_open(term->shm_name, O_CREAT | O_RDWR, 0666);
    }
    if (term->shm_fd < 0) return 1;

    if (ftruncate(term->shm_fd, (off_t)term->shm_size) != 0) {
        close(term->shm_fd);
        return 2;
    }

    term->header = (struct akaos_surface_header *)mmap(
        NULL, term->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, term->shm_fd, 0);
    if (term->header == MAP_FAILED) {
        close(term->shm_fd);
        return 3;
    }

    term->header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    term->header->width = TERM_WIDTH;
    term->header->height = TERM_HEIGHT;
    term->header->stride_pixels = TERM_WIDTH;
    term->header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    term->header->pixel_offset = sizeof(struct akaos_surface_header);
    term->header->byte_size = term->shm_size;
    term->pixels = (uint32_t *)((uint8_t *)term->header + term->header->pixel_offset);

    /* Allocate PTY pair */
    term->master_fd = posix_openpt(O_RDWR);
    if (term->master_fd < 0) {
        /* Fallback if PTY already allocated or unavailable */
        term->master_fd = open("/dev/ptmx", O_RDWR);
    }

    if (term->master_fd >= 0) {
        grantpt(term->master_fd);
        unlockpt(term->master_fd);
        char *slave_name = ptsname(term->master_fd);
        if (slave_name) {
            term->shell_pid = fork();
            if (term->shell_pid == 0) {
                setsid();
                int slave_fd = open(slave_name, O_RDWR);
                if (slave_fd >= 0) {
                    dup2(slave_fd, 0);
                    dup2(slave_fd, 1);
                    dup2(slave_fd, 2);
                    if (slave_fd > 2) close(slave_fd);
                    close(term->master_fd);
                    char *shell_argv[] = {"/bin/akash", NULL};
                    execv("/bin/akash", shell_argv);
                }
                _exit(1);
            }
        }
    }

    const char greeting[] = "akaOS Terminal 2.0 (akash)\n$ ";
    for (size_t i = 0; i < sizeof(greeting) - 1; ++i) {
        term_write_char(term, greeting[i]);
    }
    term_render(term);
    return 0;
}

static void cleanup_terminal(struct terminal_state *term) {
    if (term->header && term->header != MAP_FAILED) {
        munmap(term->header, term->shm_size);
        term->header = NULL;
    }
    if (term->shm_fd >= 0) {
        close(term->shm_fd);
        term->shm_fd = -1;
    }
    shm_unlink(term->shm_name);
    if (term->master_fd >= 0) {
        close(term->master_fd);
        term->master_fd = -1;
    }
}

static int terminal_queue_message(struct akaos_compositor_channel *channel,
                                  uint32_t type, uint32_t surface_id,
                                  const struct terminal_state *term) {
    if (!channel || !term) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = 60;
    message.y = 50;
    message.width = TERM_WIDTH;
    message.height = TERM_HEIGHT;
    message.z_order = 10;
    message.damage_w = TERM_WIDTH;
    message.damage_h = TERM_HEIGHT;
    strncpy(message.shm_name, term->shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, "Terminal", sizeof(message.title) - 1);
    strncpy(message.icon_name, "terminal", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int terminal_queue_damage(struct akaos_compositor_channel *channel,
                                 uint32_t surface_id) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.damage_w = TERM_WIDTH;
    message.damage_h = TERM_HEIGHT;
    return akaos_compositor_submit(channel, &message);
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: terminal [--once]\nRun the akaOS graphical terminal emulator.");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        puts("terminal (akaOS GUI Terminal) " AKAOS_VERSION);
        return 0;
    }

    int once = (argc == 2 && strcmp(argv[1], "--once") == 0);

    struct terminal_state term;
    memset(&term, 0, sizeof(term));
    if (start_terminal(&term) != 0) {
        puts("terminal: failed to initialize display surface or PTY");
        return 1;
    }

    /* Register with compositor if active */
    struct akaos_compositor_channel *channel = MAP_FAILED;
    size_t ipc_size = 0;
    int attached = 0;
    uint32_t surface_id = (uint32_t)getpid();
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd >= 0) {
        ipc_size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
        channel = mmap(
            NULL, ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
        close(ipc_fd);
        if (channel != MAP_FAILED && channel->abi_version == AKAOS_DISPLAY_ABI_VERSION &&
            channel->protocol_version == AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
            attached = terminal_queue_message(channel, AKAOS_DISPLAY_MSG_ATTACH,
                                               surface_id, &term);
        } else if (channel != MAP_FAILED) {
            munmap(channel, ipc_size);
            channel = MAP_FAILED;
        }
    }

    if (once) {
        puts("terminal: graphical terminal surface initialized");
        if (attached) {
            (void)terminal_queue_message(channel, AKAOS_DISPLAY_MSG_DESTROY,
                                          surface_id, &term);
            (void)poll(NULL, 0, 50);
        }
        if (channel != MAP_FAILED) munmap(channel, ipc_size);
        cleanup_terminal(&term);
        return 0;
    }

    /* Interactive update loop */
    for (int iter = 0; iter < 100; ++iter) {
        if (term.master_fd >= 0) {
            struct pollfd pfd = {.fd = term.master_fd, .events = POLLIN, .revents = 0};
            if (poll(&pfd, 1, 30) > 0 && (pfd.revents & POLLIN)) {
                char buf[128];
                ssize_t n = read(term.master_fd, buf, sizeof(buf));
                if (n > 0) {
                    for (ssize_t i = 0; i < n; ++i) {
                        term_write_char(&term, buf[i]);
                    }
                    term_render(&term);
                    if (attached) (void)terminal_queue_damage(channel, surface_id);
                }
            }
        }
        poll(NULL, 0, 40);
    }

    if (attached) {
        (void)terminal_queue_message(channel, AKAOS_DISPLAY_MSG_DESTROY,
                                      surface_id, &term);
        (void)poll(NULL, 0, 50);
    }
    if (channel != MAP_FAILED) munmap(channel, ipc_size);
    cleanup_terminal(&term);
    return 0;
}
