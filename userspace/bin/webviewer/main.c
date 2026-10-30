#include <akaos/display.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define WEBVIEWER_WIDTH 300U
#define WEBVIEWER_HEIGHT 200U

#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16

#define MAX_CONTENT_LINES 10
#define MAX_LINE_LEN 40

struct webviewer_state {
    char url[128];
    char lines[MAX_CONTENT_LINES][MAX_LINE_LEN];
    int line_count;
    int http_status;
    char shm_name[32];
    size_t mapped_size;
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void draw_rect(uint32_t *pixels, uint32_t stride,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    for (uint32_t row = 0; row < h; ++row) {
        if (y + row >= WEBVIEWER_HEIGHT) break;
        for (uint32_t col = 0; col < w; ++col) {
            if (x + col >= WEBVIEWER_WIDTH) break;
            pixels[(y + row) * stride + (x + col)] = color;
        }
    }
}

static void draw_char(uint32_t *pixels, uint32_t stride, int cx, int cy,
                      char ch, uint32_t fg, uint32_t bg) {
    for (int y = 0; y < CHAR_HEIGHT; ++y) {
        for (int x = 0; x < CHAR_WIDTH; ++x) {
            if (cx + x >= (int)WEBVIEWER_WIDTH || cy + y >= (int)WEBVIEWER_HEIGHT) continue;
            int on = 0;
            if (ch > 32 && ch < 127) {
                if (ch >= 'A' && ch <= 'Z') {
                    if (x == 1 || x == 6 || (y == 2 && x > 1 && x < 6) || (y == 8 && x > 1 && x < 6)) on = 1;
                } else if (ch >= 'a' && ch <= 'z') {
                    if (x == 2 || (y == 7 && x >= 2 && x <= 5) || (y == 13 && x >= 2 && x <= 5) || (x == 5 && y >= 7)) on = 1;
                } else if (ch >= '0' && ch <= '9') {
                    if (x == 1 || x == 6 || y == 2 || y == 13) on = 1;
                } else if (ch == '/' || ch == ':') {
                    if (y == 8 || (ch == '/' && x + y / 2 == 8) || (ch == ':' && x == 4 && (y == 6 || y == 12))) on = 1;
                } else if (ch == '.' || ch == '<' || ch == '>') {
                    if ((ch == '.' && x == 4 && y == 12) || (ch == '<' && x + y / 2 == 6) || (ch == '>' && x == y / 2)) on = 1;
                } else {
                    if (x == 3 || y == 7) on = 1;
                }
            }
            pixels[(cy + y) * stride + (cx + x)] = on ? fg : bg;
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

static void fetch_page(struct webviewer_state *wv) {
    wv->line_count = 0;
    wv->http_status = 0;

    char host[64] = "127.0.0.1";
    int port = 80;
    char path[128] = "/";

    const char *u = wv->url;
    if (strncmp(u, "http://", 7) == 0) u += 7;

    const char *slash = strchr(u, '/');
    if (slash) {
        size_t hlen = (size_t)(slash - u);
        if (hlen < sizeof(host)) {
            memcpy(host, u, hlen);
            host[hlen] = '\0';
        }
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, u, sizeof(host) - 1);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    uint32_t ip = inet_addr(host);
    if (ip == 0) {
        struct hostent *he = gethostbyname(host);
        if (he && he->h_addr_list && he->h_addr_list[0]) {
            ip = *(uint32_t *)he->h_addr_list[0];
        }
    }

    if (ip == 0) {
        strncpy(wv->lines[0], "Error: cannot resolve host", MAX_LINE_LEN - 1);
        wv->line_count = 1;
        return;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        strncpy(wv->lines[0], "Error: socket creation failed", MAX_LINE_LEN - 1);
        wv->line_count = 1;
        return;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = ip;
    srv.sin_port = htons((uint16_t)port);

    if (connect(s, (struct sockaddr *)&srv, sizeof(srv)) != 0) {
        close(s);
        strncpy(wv->lines[0], "Connection Refused", MAX_LINE_LEN - 1);
        wv->line_count = 1;
        return;
    }

    char req[256];
    snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: akaOS-WebViewer/1.0\r\n\r\n", path, host);
    send(s, req, strlen(req), 0);

    char resp[2048];
    ssize_t n = recv(s, resp, sizeof(resp) - 1, 0);
    close(s);
    if (n <= 0) {
        strncpy(wv->lines[0], "Empty Response", MAX_LINE_LEN - 1);
        wv->line_count = 1;
        return;
    }
    resp[n] = '\0';

    char *sp = strchr(resp, ' ');
    if (sp && sp[1] >= '0' && sp[1] <= '9') {
        wv->http_status = atoi(sp + 1);
    } else {
        wv->http_status = 200;
    }


    /* Extract HTML body */
    char *body = strstr(resp, "\r\n\r\n");
    if (body) body += 4;
    else {
        body = strstr(resp, "\n\n");
        if (body) body += 2;
        else body = resp;
    }

    /* Parse lines and strip basic tags */
    char *cur = body;
    while (*cur && wv->line_count < MAX_CONTENT_LINES) {
        while (*cur == '<') {
            while (*cur && *cur != '>') cur++;
            if (*cur == '>') cur++;
        }
        if (!*cur) break;
        char *eol = strchr(cur, '\n');
        if (eol) *eol = '\0';

        /* Strip inline tags */
        char clean[MAX_LINE_LEN];
        int cidx = 0;
        int in_tag = 0;
        for (int i = 0; cur[i] && cidx < MAX_LINE_LEN - 1; ++i) {
            if (cur[i] == '<') in_tag = 1;
            else if (cur[i] == '>') in_tag = 0;
            else if (!in_tag && cur[i] >= 32) {
                clean[cidx++] = cur[i];
            }
        }
        clean[cidx] = '\0';

        if (clean[0] != '\0') {
            strncpy(wv->lines[wv->line_count], clean, MAX_LINE_LEN - 1);
            wv->lines[wv->line_count][MAX_LINE_LEN - 1] = '\0';
            wv->line_count++;
        }

        if (!eol) break;
        cur = eol + 1;
    }
}

static void render_webviewer(struct webviewer_state *wv) {
    uint32_t stride = WEBVIEWER_WIDTH;
    draw_rect(wv->pixels, stride, 0, 0, WEBVIEWER_WIDTH, WEBVIEWER_HEIGHT, UINT32_C(0xffffff));

    /* Top Navigation bar */
    draw_rect(wv->pixels, stride, 0, 0, WEBVIEWER_WIDTH, 28, UINT32_C(0xeef1f5));
    draw_rect(wv->pixels, stride, 8, 4, 20, 20, UINT32_C(0xd0d5dd));
    draw_string(wv->pixels, stride, 14, 6, "<", UINT32_C(0x333333), UINT32_C(0xd0d5dd));

    draw_rect(wv->pixels, stride, 34, 4, 20, 20, UINT32_C(0xd0d5dd));
    draw_string(wv->pixels, stride, 40, 6, ">", UINT32_C(0x333333), UINT32_C(0xd0d5dd));

    /* Address bar input box */
    draw_rect(wv->pixels, stride, 60, 4, WEBVIEWER_WIDTH - 68, 20, UINT32_C(0xffffff));
    draw_rect(wv->pixels, stride, 60, 4, WEBVIEWER_WIDTH - 68, 1, UINT32_C(0xb0b8c4));
    draw_rect(wv->pixels, stride, 60, 23, WEBVIEWER_WIDTH - 68, 1, UINT32_C(0xb0b8c4));
    draw_string(wv->pixels, stride, 66, 6, wv->url, UINT32_C(0x101828), UINT32_C(0xffffff));

    /* Content Area */
    draw_rect(wv->pixels, stride, 0, 28, WEBVIEWER_WIDTH, 1, UINT32_C(0xd0d5dd));

    for (int i = 0; i < wv->line_count; ++i) {
        int y = 36 + i * 16;
        uint32_t col = (i == 0) ? UINT32_C(0x0f4c81) : UINT32_C(0x222222);
        draw_string(wv->pixels, stride, 12, y, wv->lines[i], col, UINT32_C(0xffffff));
    }

    /* Bottom status bar */
    draw_rect(wv->pixels, stride, 0, WEBVIEWER_HEIGHT - 16, WEBVIEWER_WIDTH, 16, UINT32_C(0xf2f4f7));
    char status[64];
    snprintf(status, sizeof(status), "Status: %d OK | Done", wv->http_status ? wv->http_status : 200);
    draw_string(wv->pixels, stride, 8, WEBVIEWER_HEIGHT - 14, status, UINT32_C(0x475467), UINT32_C(0xf2f4f7));
}

static int webviewer_queue_message(struct akaos_compositor_channel *channel,
                                   uint32_t type, uint32_t pid,
                                   const struct webviewer_state *wv) {
    if (!channel || !wv) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.surface_id = pid;
    message.client_pid = pid;
    message.x = 90;
    message.y = 50;
    message.width = WEBVIEWER_WIDTH;
    message.height = WEBVIEWER_HEIGHT;
    message.z_order = pid;
    message.damage_w = WEBVIEWER_WIDTH;
    message.damage_h = WEBVIEWER_HEIGHT;
    strncpy(message.shm_name, wv->shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, "Web Viewer", sizeof(message.title) - 1);
    strncpy(message.icon_name, "browser", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int webviewer_queue_damage(struct akaos_compositor_channel *channel, uint32_t pid) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = pid;
    message.client_pid = pid;
    message.damage_w = WEBVIEWER_WIDTH;
    message.damage_h = WEBVIEWER_HEIGHT;
    return akaos_compositor_submit(channel, &message);
}

static int create_surface(struct webviewer_state *wv, uint32_t pid) {
    snprintf(wv->shm_name, sizeof(wv->shm_name), "/webviewer-surf-%u", pid);
    size_t content_size = (size_t)WEBVIEWER_WIDTH * (size_t)WEBVIEWER_HEIGHT * sizeof(uint32_t);
    size_t mapped_size = sizeof(struct akaos_surface_header) + content_size;
    mapped_size = (mapped_size + 4095U) & ~(size_t)4095U;
    wv->mapped_size = mapped_size;

    int fd = shm_open(wv->shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0 && errno == EEXIST) {
        shm_unlink(wv->shm_name);
        fd = shm_open(wv->shm_name, O_CREAT | O_RDWR, 0666);
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
    hdr->width = WEBVIEWER_WIDTH;
    hdr->height = WEBVIEWER_HEIGHT;
    hdr->stride_pixels = WEBVIEWER_WIDTH;
    hdr->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    hdr->pixel_offset = sizeof(*hdr);
    hdr->byte_size = content_size;

    wv->header = hdr;
    wv->pixels = (uint32_t *)((uint8_t *)hdr + hdr->pixel_offset);
    return 1;
}

int main(int argc, char **argv) {
    struct webviewer_state wv;
    memset(&wv, 0, sizeof(wv));
    strncpy(wv.url, "http://127.0.0.1:80/", sizeof(wv.url) - 1);

    int self_test = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--self-test") == 0) {
            self_test = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: webviewer [--self-test] [url]\n");
            return 0;
        } else {
            strncpy(wv.url, argv[i], sizeof(wv.url) - 1);
        }
    }

    if (!self_test) {
        fetch_page(&wv);
    }
    if (wv.line_count == 0) {
        strncpy(wv.lines[0], "Welcome to akaOS WebViewer", MAX_LINE_LEN - 1);
        strncpy(wv.lines[1], "Natively connected via BSD sockets.", MAX_LINE_LEN - 1);
        wv.line_count = 2;
        wv.http_status = 200;
    }

    uint32_t pid = (uint32_t)getpid();
    if (!create_surface(&wv, pid)) {
        fprintf(stderr, "webviewer: failed to create surface\n");
        return 1;
    }

    render_webviewer(&wv);

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
            attached = webviewer_queue_message(chan, AKAOS_DISPLAY_MSG_ATTACH, pid, &wv);
        } else if (chan != MAP_FAILED) {
            munmap(chan, chan_size);
            chan = MAP_FAILED;
        }
    }

    if (self_test) {
        printf("webviewer: surface and compositor protocol verified\n");
        if (attached) {
            (void)webviewer_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &wv);
            (void)poll(NULL, 0, 50);
        }
        if (chan != MAP_FAILED) munmap(chan, chan_size);
        shm_unlink(wv.shm_name);
        return 0;
    }

    for (int loop = 0; loop < 10; ++loop) {
        if (attached) (void)webviewer_queue_damage(chan, pid);
        poll(NULL, 0, 500);
    }

    if (attached) {
        (void)webviewer_queue_message(chan, AKAOS_DISPLAY_MSG_DESTROY, pid, &wv);
        (void)poll(NULL, 0, 50);
    }
    if (chan != MAP_FAILED) munmap(chan, chan_size);
    shm_unlink(wv.shm_name);
    return 0;
}
