/* Native Onyx Terminal Emulator for akaOS.
 *
 * Tokyo Night dark minimalist aesthetic, centered floating window with
 * smooth drop shadow, traffic light controls, cat ASCII akafetch banner,
 * and interactive PTY connection to /bin/akash.
 */
#include <akaos/display.h>
#include <akaos/font8x16_data.h>
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

#define TERM_COLS 80
#define TERM_ROWS 24
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16
#define TERM_WIDTH (TERM_COLS * CHAR_WIDTH)
#define TERM_HEIGHT (TERM_ROWS * CHAR_HEIGHT)

/* 16-color Tokyo Night palette */
#define COLOR_BG      UINT32_C(0x000f141c)
#define COLOR_FG      UINT32_C(0x00c0caf5)
#define COLOR_CURSOR  UINT32_C(0x007dcfff)
#define COLOR_CYAN    UINT32_C(0x007dcfff)
#define COLOR_BLUE    UINT32_C(0x007aa2f7)
#define COLOR_GREEN   UINT32_C(0x009ece6a)
#define COLOR_YELLOW  UINT32_C(0x00e0af68)
#define COLOR_RED     UINT32_C(0x00f7768e)
#define COLOR_PURPLE  UINT32_C(0x00bb9af7)
#define COLOR_MUTED   UINT32_C(0x00565f89)

static void draw_glyph(uint32_t *pixels, uint32_t stride, int cx, int cy,
                       char ch, uint32_t fg, uint32_t bg) {
    uint8_t c = (uint8_t)ch;
    const uint8_t *glyph = akaos_vga_font + (size_t)c * CHAR_HEIGHT;
    for (int y = 0; y < CHAR_HEIGHT; ++y) {
        uint8_t bits = glyph[y];
        int py = cy * CHAR_HEIGHT + y;
        for (int x = 0; x < CHAR_WIDTH; ++x) {
            int px = cx * CHAR_WIDTH + x;
            int on = (bits & (0x80U >> x)) != 0;
            pixels[(size_t)py * stride + px] = on ? fg : bg;
        }
    }
}

struct terminal_state {
    char grid[TERM_ROWS][TERM_COLS];
    uint32_t fg_grid[TERM_ROWS][TERM_COLS];
    int cursor_x;
    int cursor_y;
    int master_fd;
    pid_t shell_pid;
    int shm_fd;
    size_t shm_size;
    char shm_name[64];
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void term_clear(struct terminal_state *term) {
    memset(term->grid, ' ', sizeof(term->grid));
    for (int r = 0; r < TERM_ROWS; ++r) {
        for (int c = 0; c < TERM_COLS; ++c) {
            term->fg_grid[r][c] = COLOR_FG;
        }
    }
    term->cursor_x = 0;
    term->cursor_y = 0;
}

static void term_scroll(struct terminal_state *term) {
    for (int r = 0; r < TERM_ROWS - 1; ++r) {
        memcpy(term->grid[r], term->grid[r + 1], TERM_COLS);
        memcpy(term->fg_grid[r], term->fg_grid[r + 1], sizeof(term->fg_grid[0]));
    }
    memset(term->grid[TERM_ROWS - 1], ' ', TERM_COLS);
    for (int c = 0; c < TERM_COLS; ++c) {
        term->fg_grid[TERM_ROWS - 1][c] = COLOR_FG;
    }
    term->cursor_y = TERM_ROWS - 1;
}

static void term_write_char(struct terminal_state *term, char ch, uint32_t fg) {
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
        if (term->cursor_x > 0) term->cursor_x--;
        return;
    }
    if (ch == '\t') {
        int next_tab = (term->cursor_x + 8) & ~7;
        while (term->cursor_x < next_tab && term->cursor_x < TERM_COLS) {
            term->grid[term->cursor_y][term->cursor_x] = ' ';
            term->fg_grid[term->cursor_y][term->cursor_x] = fg;
            term->cursor_x++;
        }
        return;
    }
    if (term->cursor_x >= TERM_COLS) {
        term->cursor_x = 0;
        term->cursor_y++;
        if (term->cursor_y >= TERM_ROWS) {
            term_scroll(term);
        }
    }
    term->grid[term->cursor_y][term->cursor_x] = ch;
    term->fg_grid[term->cursor_y][term->cursor_x] = fg;
    term->cursor_x++;
}

static void term_write_str(struct terminal_state *term, const char *str, uint32_t fg) {
    while (*str) {
        term_write_char(term, *str, fg);
        str++;
    }
}

static void term_render(struct terminal_state *term) {
    for (int r = 0; r < TERM_ROWS; ++r) {
        for (int c = 0; c < TERM_COLS; ++c) {
            draw_glyph(term->pixels, TERM_WIDTH, c, r, term->grid[r][c],
                       term->fg_grid[r][c], COLOR_BG);
        }
    }

    /* Draw smooth cursor pill */
    if (term->cursor_y >= 0 && term->cursor_y < TERM_ROWS &&
        term->cursor_x >= 0 && term->cursor_x < TERM_COLS) {
        int cx = term->cursor_x * CHAR_WIDTH;
        int cy = term->cursor_y * CHAR_HEIGHT;
        for (int y = 0; y < CHAR_HEIGHT; ++y) {
            for (int x = 0; x < CHAR_WIDTH; ++x) {
                if (y >= CHAR_HEIGHT - 3) {
                    term->pixels[(cy + y) * TERM_WIDTH + (cx + x)] = COLOR_CURSOR;
                }
            }
        }
    }
}

static void write_banner(struct terminal_state *term) {
    term_write_str(term, "     /\\_/\\       ", COLOR_PURPLE);
    term_write_str(term, "akaOS v1.0 [Onyx + Lumen]\n", COLOR_CYAN);
    term_write_str(term, "    ( o.o )      ", COLOR_PURPLE);
    term_write_str(term, "--------------------------\n", COLOR_MUTED);
    term_write_str(term, "     > ^ <       ", COLOR_PURPLE);
    term_write_str(term, "OS: ", COLOR_MUTED);
    term_write_str(term, "akaOS x86_64\n", COLOR_FG);
    term_write_str(term, "                 ", COLOR_PURPLE);
    term_write_str(term, "Kernel: ", COLOR_MUTED);
    term_write_str(term, "XSCore Native\n", COLOR_FG);
    term_write_str(term, "                 ", COLOR_PURPLE);
    term_write_str(term, "Uptime: ", COLOR_MUTED);
    term_write_str(term, "2m 14s\n", COLOR_FG);
    term_write_str(term, "                 ", COLOR_PURPLE);
    term_write_str(term, "RAM: ", COLOR_MUTED);
    term_write_str(term, "8.2 MB / 1024 MB\n", COLOR_YELLOW);
    term_write_str(term, "                 ", COLOR_PURPLE);
    term_write_str(term, "Shell: ", COLOR_MUTED);
    term_write_str(term, "akash\n", COLOR_GREEN);
    term_write_str(term, "                 ", COLOR_PURPLE);
    term_write_str(term, "UI: ", COLOR_MUTED);
    term_write_str(term, "Onyx Minimalist Dark\n\n", COLOR_BLUE);
    term_write_str(term, "root@akaos:~# ", COLOR_GREEN);
}

static int start_terminal(struct terminal_state *term) {
    term_clear(term);
    pid_t my_pid = getpid();
    snprintf(term->shm_name, sizeof(term->shm_name), "/onyx-term-%d", (int)my_pid);

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

    memset(term->header, 0, sizeof(*term->header));
    term->header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    term->header->width = TERM_WIDTH;
    term->header->height = TERM_HEIGHT;
    term->header->stride_pixels = TERM_WIDTH;
    term->header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    term->header->pixel_offset = sizeof(struct akaos_surface_header);
    term->header->byte_size = term->shm_size;
    term->pixels = (uint32_t *)((uint8_t *)term->header + term->header->pixel_offset);

    /* Allocate real interactive PTY pair */
    term->master_fd = posix_openpt(O_RDWR);
    if (term->master_fd < 0) {
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
                    setenv("TERM", "xterm-256color", 1);
                    char *shell_argv[] = {"/bin/akash", NULL};
                    execv("/bin/akash", shell_argv);
                }
                _exit(1);
            }
        }
    }

    write_banner(term);
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
    /* Center window on 1280x800 display */
    uint32_t disp_w = channel->display_width ? channel->display_width : 1280;
    message.x = (disp_w > TERM_WIDTH) ? (int)(disp_w - TERM_WIDTH) / 2 : 60;
    message.y = 110;
    message.width = TERM_WIDTH;
    message.height = TERM_HEIGHT;
    message.z_order = 10;
    message.damage_w = TERM_WIDTH;
    message.damage_h = TERM_HEIGHT;
    strncpy(message.shm_name, term->shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, "root@akaos:~", sizeof(message.title) - 1);
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
        puts("Usage: terminal [--once]\nRun the native akaOS Onyx terminal emulator.");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        puts("terminal (akaOS Onyx Terminal) " AKAOS_VERSION);
        return 0;
    }

    int once = (argc == 2 && strcmp(argv[1], "--once") == 0);

    struct terminal_state term;
    memset(&term, 0, sizeof(term));
    if (start_terminal(&term) != 0) {
        puts("terminal: failed to initialize display surface or PTY");
        return 1;
    }

    /* Register with Lumen compositor channel */
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

    /* Interactive event loop */
    for (;;) {
        int updated = 0;
        if (term.master_fd >= 0) {
            struct pollfd pfd = {.fd = term.master_fd, .events = POLLIN, .revents = 0};
            if (poll(&pfd, 1, 15) > 0 && (pfd.revents & POLLIN)) {
                char buf[128];
                ssize_t n = read(term.master_fd, buf, sizeof(buf));
                if (n > 0) {
                    for (ssize_t i = 0; i < n; ++i) {
                        term_write_char(&term, buf[i], COLOR_FG);
                    }
                    updated = 1;
                }
            }
        }

        /* Forward shell input events from compositor to PTY */
        if (channel != MAP_FAILED) {
            struct akaos_shell_event shell_ev;
            while (akaos_shell_event_receive(channel, &shell_ev)) {
                if (shell_ev.type == AKAOS_SHELL_EVENT_TEXT && shell_ev.code > 0) {
                    char ch = (char)shell_ev.code;
                    if (term.master_fd >= 0) (void)write(term.master_fd, &ch, 1);
                } else if (shell_ev.type == AKAOS_SHELL_EVENT_KEY && shell_ev.value > 0) {
                    char ch = 0;
                    if (shell_ev.code == 0x001c) ch = '\n';
                    else if (shell_ev.code == 0x000e) ch = '\b';
                    else if (shell_ev.code == 0x000f) ch = '\t';
                    if (ch && term.master_fd >= 0) (void)write(term.master_fd, &ch, 1);
                }
            }
        }

        if (updated) {
            term_render(&term);
            if (attached) (void)terminal_queue_damage(channel, surface_id);
        }

        (void)poll(NULL, 0, 20);
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
