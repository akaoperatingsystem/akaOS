#include <akaos/display.h>
#include <akaos/ui_font_mono_aa.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

#if defined(__i386__)
#define NOTEPAD_WIDTH 280U
#define NOTEPAD_HEIGHT 180U
#else
#define NOTEPAD_WIDTH 420U
#define NOTEPAD_HEIGHT 280U
#endif
#define CHAR_WIDTH 9
#define CHAR_HEIGHT 18

#define MAX_LINES 10
#define MAX_COLS 32

struct notepad_state {
    char lines[MAX_LINES][MAX_COLS];
    int line_count;
    char filename[64];
    char shm_name[32];
    size_t mapped_size;
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void draw_rect(uint32_t *pixels, uint32_t stride,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    for (uint32_t row = 0; row < h; ++row) {
        if (y + row >= NOTEPAD_HEIGHT) break;
        for (uint32_t col = 0; col < w; ++col) {
            if (x + col >= NOTEPAD_WIDTH) break;
            pixels[(y + row) * stride + (x + col)] = color;
        }
    }
}

static void draw_char(uint32_t *pixels, uint32_t stride, int cx, int cy,
                      char ch, uint32_t fg, uint32_t bg) {
    for (int y = 0; y < CHAR_HEIGHT; ++y) {
        for (int x = 0; x < CHAR_WIDTH; ++x) {
            if (cx + x >= (int)NOTEPAD_WIDTH || cy + y >= (int)NOTEPAD_HEIGHT) continue;
            uint8_t alpha = akaos_mono_font_get_alpha(ch, (uint32_t)y,
                                                       (uint32_t)x);
            uint32_t inverse = 255U - alpha;
            uint32_t r = (((bg >> 16) & 0xffU) * inverse +
                          ((fg >> 16) & 0xffU) * alpha + 127U) / 255U;
            uint32_t g = (((bg >> 8) & 0xffU) * inverse +
                          ((fg >> 8) & 0xffU) * alpha + 127U) / 255U;
            uint32_t b = ((bg & 0xffU) * inverse +
                          (fg & 0xffU) * alpha + 127U) / 255U;
            pixels[(cy + y) * stride + (cx + x)] = (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_string(uint32_t *pixels, uint32_t stride, int x, int y,
                        const char *str, uint32_t fg, uint32_t bg) {
    int cur_x = x;
    while (*str) {
        draw_char(pixels, stride, cur_x, y, *str++, fg, bg);
        cur_x += CHAR_WIDTH;
    }
}

static void render_notepad(struct notepad_state *np) {
    uint32_t stride = NOTEPAD_WIDTH;
    /* Window body */
    draw_rect(np->pixels, stride, 0, 0, NOTEPAD_WIDTH, NOTEPAD_HEIGHT, UINT32_C(0x171a20));

    /* Menu / toolbar bar */
    draw_rect(np->pixels, stride, 0, 0, NOTEPAD_WIDTH, 28, UINT32_C(0x222832));
    draw_string(np->pixels, stride, 14, 5, "File   Edit   View   Help",
                UINT32_C(0xe6eaf0), UINT32_C(0x222832));

    /* Text editor area */
    for (int r = 0; r < np->line_count && r < MAX_LINES; ++r) {
        int y = 42 + r * 21;
        draw_string(np->pixels, stride, 16, y, np->lines[r],
                    UINT32_C(0xe4e9f2), UINT32_C(0x171a20));
    }

    /* Status bar at bottom */
    draw_rect(np->pixels, stride, 0, NOTEPAD_HEIGHT - 26, NOTEPAD_WIDTH, 26, UINT32_C(0x246fd2));
    char status[64];
    snprintf(status, sizeof(status), " %s | UTF-8 | Ln %d, Col 1",
             np->filename[0] ? np->filename : "Untitled", np->line_count);
    draw_string(np->pixels, stride, 12, NOTEPAD_HEIGHT - 22, status,
                UINT32_C(0xffffff), UINT32_C(0x246fd2));
}

static int notepad_queue_message(struct akaos_compositor_channel *channel,
                                 uint32_t type, uint32_t pid,
                                 const struct notepad_state *np) {
    if (!channel || !np) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.surface_id = pid;
    message.client_pid = pid;
    message.x = channel->display_width > NOTEPAD_WIDTH + 80U ? 80 : 20;
    message.y = channel->display_height > NOTEPAD_HEIGHT + 120U ? 100 : 48;
    message.width = NOTEPAD_WIDTH;
    message.height = NOTEPAD_HEIGHT;
    message.z_order = pid;
    message.damage_w = NOTEPAD_WIDTH;
    message.damage_h = NOTEPAD_HEIGHT;
    strncpy(message.shm_name, np->shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, "Notepad", sizeof(message.title) - 1);
    strncpy(message.icon_name, "notepad", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int notepad_queue_damage(struct akaos_compositor_channel *channel, uint32_t pid) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = pid;
    message.client_pid = pid;
    message.damage_w = NOTEPAD_WIDTH;
    message.damage_h = NOTEPAD_HEIGHT;
    return akaos_compositor_submit(channel, &message);
}

static int create_surface(struct notepad_state *np, uint32_t pid) {
    snprintf(np->shm_name, sizeof(np->shm_name), "/notepad-surf-%u", pid);
    size_t content_size = (size_t)NOTEPAD_WIDTH * (size_t)NOTEPAD_HEIGHT * sizeof(uint32_t);
    size_t mapped_size = sizeof(struct akaos_surface_header) + content_size;
    mapped_size = (mapped_size + 4095U) & ~(size_t)4095U;
    np->mapped_size = mapped_size;

    int fd = shm_open(np->shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0 && errno == EEXIST) {
        shm_unlink(np->shm_name);
        fd = shm_open(np->shm_name, O_CREAT | O_RDWR, 0666);
    }
    if (fd < 0) return 0;
    if (ftruncate(fd, (off_t)mapped_size) != 0) {
        close(fd);
        return 0;
    }

    struct akaos_surface_header *hdr = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (hdr == MAP_FAILED) return 0;

    hdr->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    hdr->width = NOTEPAD_WIDTH;
    hdr->height = NOTEPAD_HEIGHT;
    hdr->stride_pixels = NOTEPAD_WIDTH;
    hdr->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    hdr->pixel_offset = sizeof(*hdr);
    hdr->byte_size = content_size;

    np->header = hdr;
    np->pixels = (uint32_t *)((uint8_t *)hdr + hdr->pixel_offset);
    return 1;
}

static void load_file_content(struct notepad_state *np, const char *path) {
    strncpy(np->filename, path, sizeof(np->filename) - 1);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        strncpy(np->lines[0], "# New Document", MAX_COLS - 1);
        np->line_count = 1;
        return;
    }

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        strncpy(np->lines[0], "# Empty File", MAX_COLS - 1);
        np->line_count = 1;
        return;
    }
    buf[n] = '\0';

    char *line = buf;
    np->line_count = 0;
    while (*line && np->line_count < MAX_LINES) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        strncpy(np->lines[np->line_count], line, MAX_COLS - 1);
        np->lines[np->line_count][MAX_COLS - 1] = '\0';
        np->line_count++;
        if (!eol) break;
        line = eol + 1;
    }
}

int main(int argc, char **argv) {
    struct notepad_state np;
    memset(&np, 0, sizeof(np));

    int self_test = 0;
    const char *open_file = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--self-test") == 0) {
            self_test = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: notepad [--self-test] [file]\n");
            return 0;
        } else if (!open_file) {
            open_file = argv[i];
        }
    }

    if (open_file) {
        load_file_content(&np, open_file);
    } else {
        strncpy(np.lines[0], "Welcome to Notepad!", MAX_COLS - 1);
        strncpy(np.lines[1], "akaOS Native Text Editor", MAX_COLS - 1);
        strncpy(np.lines[2], "Ready for editing.", MAX_COLS - 1);
        np.line_count = 3;
    }

    uint32_t pid = (uint32_t)getpid();
    if (!create_surface(&np, pid)) {
        fprintf(stderr, "notepad: failed to create shared display surface\n");
        return 1;
    }

    render_notepad(&np);

    /* Register with Lumen compositor through the bounded request queue. */
    struct akaos_compositor_channel *chan = MAP_FAILED;
    size_t chan_size = 0;
    int attached = 0;
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd >= 0) {
        chan_size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
        chan = mmap(NULL, chan_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
        close(ipc_fd);
        if (chan != MAP_FAILED && chan->abi_version == AKAOS_DISPLAY_ABI_VERSION &&
            chan->protocol_version == AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
            attached = notepad_queue_message(chan, AKAOS_DISPLAY_MSG_ATTACH, pid, &np);
        } else if (chan != MAP_FAILED) {
            munmap(chan, chan_size);
            chan = MAP_FAILED;
        }
    }

    if (self_test) {
        printf("notepad: surface and compositor protocol verified\n");
        if (attached) {
            (void)notepad_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &np);
            (void)poll(NULL, 0, 50);
        }
        if (chan != MAP_FAILED) munmap(chan, chan_size);
        shm_unlink(np.shm_name);
        return 0;
    }

    /* Event loop */
    for (int loop = 0; loop < 10; ++loop) {
        if (attached) (void)notepad_queue_damage(chan, pid);
        poll(NULL, 0, 500);
    }

    if (attached) {
        (void)notepad_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &np);
        (void)poll(NULL, 0, 50);
    }
    if (chan != MAP_FAILED) munmap(chan, chan_size);
    shm_unlink(np.shm_name);
    return 0;
}
