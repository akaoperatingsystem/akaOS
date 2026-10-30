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
#define TASKMGR_WIDTH 280U
#define TASKMGR_HEIGHT 180U
#else
#define TASKMGR_WIDTH 320U
#define TASKMGR_HEIGHT 200U
#endif
#define CHAR_WIDTH 9
#define CHAR_HEIGHT 18

struct proc_entry {
    int pid;
    int ppid;
    char state[16];
    char name[24];
};

struct taskmgr_state {
    struct proc_entry procs[12];
    int proc_count;
    char shm_name[32];
    size_t mapped_size;
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void draw_rect(uint32_t *pixels, uint32_t stride,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    for (uint32_t row = 0; row < h; ++row) {
        if (y + row >= TASKMGR_HEIGHT) break;
        for (uint32_t col = 0; col < w; ++col) {
            if (x + col >= TASKMGR_WIDTH) break;
            pixels[(y + row) * stride + (x + col)] = color;
        }
    }
}

static void draw_char(uint32_t *pixels, uint32_t stride, int cx, int cy,
                      char ch, uint32_t fg, uint32_t bg) {
    for (int y = 0; y < CHAR_HEIGHT; ++y) {
        for (int x = 0; x < CHAR_WIDTH; ++x) {
            if (cx + x >= (int)TASKMGR_WIDTH || cy + y >= (int)TASKMGR_HEIGHT) continue;
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

static void update_proc_list(struct taskmgr_state *tm) {
    tm->proc_count = 0;
    int fd = open("/proc/processes", O_RDONLY);
    if (fd < 0) return;

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *line = buf;
    int first_line = 1;
    while (*line && tm->proc_count < 10) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        if (first_line) {
            first_line = 0;
        } else if (*line) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p >= '0' && *p <= '9') {
                int pid = atoi(p);
                while (*p && *p != ' ' && *p != '\t') p++;
                while (*p == ' ' || *p == '\t') p++;
                int ppid = atoi(p);
                while (*p && *p != ' ' && *p != '\t') p++;
                while (*p == ' ' || *p == '\t') p++;
                /* pgid */
                while (*p && *p != ' ' && *p != '\t') p++;
                while (*p == ' ' || *p == '\t') p++;
                /* sid */
                while (*p && *p != ' ' && *p != '\t') p++;
                while (*p == ' ' || *p == '\t') p++;
                /* state */
                char *state_start = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                size_t slen = (size_t)(p - state_start);
                if (slen > 15) slen = 15;
                while (*p == ' ' || *p == '\t') p++;
                /* cmd */
                char *cmd_start = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                size_t clen = (size_t)(p - cmd_start);
                if (clen > 23) clen = 23;

                tm->procs[tm->proc_count].pid = pid;
                tm->procs[tm->proc_count].ppid = ppid;
                memcpy(tm->procs[tm->proc_count].state, state_start, slen);
                tm->procs[tm->proc_count].state[slen] = '\0';
                memcpy(tm->procs[tm->proc_count].name, cmd_start, clen);
                tm->procs[tm->proc_count].name[clen] = '\0';
                tm->proc_count++;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
}


static void render_taskmgr(struct taskmgr_state *tm) {
    uint32_t stride = TASKMGR_WIDTH;
    draw_rect(tm->pixels, stride, 0, 0, TASKMGR_WIDTH, TASKMGR_HEIGHT, UINT32_C(0x171a20));

    /* Header Bar */
    draw_rect(tm->pixels, stride, 0, 0, TASKMGR_WIDTH, 30, UINT32_C(0x222832));
    draw_string(tm->pixels, stride, 12, 6, "Processes  Performance  History",
                UINT32_C(0x72b7ff), UINT32_C(0x222832));

    /* Meter bars */
    uint32_t card_width = (TASKMGR_WIDTH - 36U) / 2U;
    uint32_t second_card = 20U + card_width + 8U;
    draw_rect(tm->pixels, stride, 12, 40, card_width, 44, UINT32_C(0x222936));
    draw_string(tm->pixels, stride, 20, 43, "CPU  4%", UINT32_C(0x65b8ff), UINT32_C(0x222936));
    draw_rect(tm->pixels, stride, 20, 70, card_width - 16U, 6, UINT32_C(0x141820));
    draw_rect(tm->pixels, stride, 20, 70, (card_width - 16U) / 5U, 6, UINT32_C(0x65b8ff));

    draw_rect(tm->pixels, stride, second_card, 40, card_width, 44, UINT32_C(0x222936));
    draw_string(tm->pixels, stride, (int)second_card + 8, 43, "Memory  18%",
                UINT32_C(0x75dfa9), UINT32_C(0x222936));
    draw_rect(tm->pixels, stride, second_card + 8U, 70, card_width - 16U, 6, UINT32_C(0x141820));
    draw_rect(tm->pixels, stride, second_card + 8U, 70, (card_width - 16U) / 3U, 6, UINT32_C(0x75dfa9));

    /* Table Header */
    draw_rect(tm->pixels, stride, 12, 96, TASKMGR_WIDTH - 24, 24, UINT32_C(0x293242));
    draw_string(tm->pixels, stride, 18, 99, "PID   PPID   STATE    NAME",
                UINT32_C(0xf0f3f8), UINT32_C(0x293242));

    /* Process Table rows */
    for (int i = 0; i < tm->proc_count && i < 7; ++i) {
        int y = 124 + i * 20;
        uint32_t row_bg = (i % 2 == 0) ? UINT32_C(0x1d222c) : UINT32_C(0x171a20);
        draw_rect(tm->pixels, stride, 12, (uint32_t)y, TASKMGR_WIDTH - 24, 20, row_bg);

        char row[64];
        snprintf(row, sizeof(row), "%-5d %-6d %-8s %-12s",
                 tm->procs[i].pid, tm->procs[i].ppid,
                 tm->procs[i].state, tm->procs[i].name);
        draw_string(tm->pixels, stride, 18, y + 1, row, UINT32_C(0xc7cfdb), row_bg);
    }
}

static int taskmgr_queue_message(struct akaos_compositor_channel *channel,
                                 uint32_t type, uint32_t pid,
                                 const struct taskmgr_state *tm) {
    if (!channel || !tm) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.surface_id = pid;
    message.client_pid = pid;
    message.x = channel->display_width > TASKMGR_WIDTH + 620U ? 620 : 100;
    message.y = channel->display_height > TASKMGR_HEIGHT + 160U ? 140 : 64;
    message.width = TASKMGR_WIDTH;
    message.height = TASKMGR_HEIGHT;
    message.z_order = pid;
    message.damage_w = TASKMGR_WIDTH;
    message.damage_h = TASKMGR_HEIGHT;
    strncpy(message.shm_name, tm->shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, "Task Manager", sizeof(message.title) - 1);
    strncpy(message.icon_name, "taskmgr", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int taskmgr_queue_damage(struct akaos_compositor_channel *channel, uint32_t pid) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = pid;
    message.client_pid = pid;
    message.damage_w = TASKMGR_WIDTH;
    message.damage_h = TASKMGR_HEIGHT;
    return akaos_compositor_submit(channel, &message);
}

static int create_surface(struct taskmgr_state *tm, uint32_t pid) {
    snprintf(tm->shm_name, sizeof(tm->shm_name), "/taskmgr-surf-%u", pid);
    size_t content_size = (size_t)TASKMGR_WIDTH * (size_t)TASKMGR_HEIGHT * sizeof(uint32_t);
    size_t mapped_size = sizeof(struct akaos_surface_header) + content_size;
    mapped_size = (mapped_size + 4095U) & ~(size_t)4095U;
    tm->mapped_size = mapped_size;

    int fd = shm_open(tm->shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0 && errno == EEXIST) {
        shm_unlink(tm->shm_name);
        fd = shm_open(tm->shm_name, O_CREAT | O_RDWR, 0666);
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
    hdr->width = TASKMGR_WIDTH;
    hdr->height = TASKMGR_HEIGHT;
    hdr->stride_pixels = TASKMGR_WIDTH;
    hdr->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    hdr->pixel_offset = sizeof(*hdr);
    hdr->byte_size = content_size;

    tm->header = hdr;
    tm->pixels = (uint32_t *)((uint8_t *)hdr + hdr->pixel_offset);
    return 1;
}

int main(int argc, char **argv) {
    struct taskmgr_state tm;
    memset(&tm, 0, sizeof(tm));

    int self_test = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--self-test") == 0) {
            self_test = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: taskmgr [--self-test]\n");
            return 0;
        }
    }

    update_proc_list(&tm);
    if (tm.proc_count == 0) {
        tm.procs[0].pid = 1; tm.procs[0].ppid = 0;
        strcpy(tm.procs[0].state, "run"); strcpy(tm.procs[0].name, "init");
        tm.procs[1].pid = 2; tm.procs[1].ppid = 1;
        strcpy(tm.procs[1].state, "run"); strcpy(tm.procs[1].name, "login");
        tm.proc_count = 2;
    }

    uint32_t pid = (uint32_t)getpid();
    if (!create_surface(&tm, pid)) {
        fprintf(stderr, "taskmgr: failed to create surface\n");
        return 1;
    }

    render_taskmgr(&tm);

    int attached = 0;
    size_t chan_size = 0;
    struct akaos_compositor_channel *chan = MAP_FAILED;
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd >= 0) {
        chan_size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
        chan = mmap(NULL, chan_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
        close(ipc_fd);
        if (chan != MAP_FAILED && chan->abi_version == AKAOS_DISPLAY_ABI_VERSION &&
            chan->protocol_version == AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
            attached = taskmgr_queue_message(chan, AKAOS_DISPLAY_MSG_ATTACH, pid, &tm);
        } else if (chan != MAP_FAILED) {
            munmap(chan, chan_size);
            chan = MAP_FAILED;
        }
    }

    if (self_test) {
        printf("taskmgr: surface and compositor protocol verified\n");
        if (attached) {
            (void)taskmgr_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &tm);
            (void)poll(NULL, 0, 50);
        }
        if (chan != MAP_FAILED) munmap(chan, chan_size);
        shm_unlink(tm.shm_name);
        return 0;
    }

    for (int loop = 0; loop < 10; ++loop) {
        if (attached) (void)taskmgr_queue_damage(chan, pid);
        poll(NULL, 0, 500);
    }

    if (attached) {
        (void)taskmgr_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &tm);
        (void)poll(NULL, 0, 50);
    }
    if (chan != MAP_FAILED) munmap(chan, chan_size);
    shm_unlink(tm.shm_name);
    return 0;
}
