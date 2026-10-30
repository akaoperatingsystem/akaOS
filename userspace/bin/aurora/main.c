#include <akaos/display.h>
#include <akaos/input.h>
#include <akaos/ui_font_aa.h>
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

#define PANEL_HEIGHT 48U
#define TASK_WIDTH 42U
#define TASK_GAP 8U
#define LAUNCHER_WIDTH 42U
#define AURORA_MAX_APPS 12U
#define AURORA_SEARCH_MAX 31U

static int g_light_theme;

struct aurora_clock {
    int month, day, year, hour, minute, second;
    unsigned subticks;
    int valid;
};

struct aurora_capabilities {
    int network;
    int network_connected;
    int audio;
    int battery;
};

struct aurora_app {
    char id[20];
    char name[32];
    char executable[48];
    char icon[20];
};

struct aurora_launcher {
    struct aurora_app apps[AURORA_MAX_APPS];
    unsigned app_count;
    char search[AURORA_SEARCH_MAX + 1U];
    unsigned search_length;
    unsigned selected;
    int open;
};

static int read_text_file(const char *path, char *buffer, size_t capacity) {
    if (!path || !buffer || capacity < 2U) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t size = read(fd, buffer, capacity - 1U);
    close(fd);
    if (size <= 0) return 0;
    buffer[size] = '\0';
    return 1;
}

static int load_light_theme(void) {
    char config[1024];
    return read_text_file("/etc/akaos.conf", config, sizeof(config)) &&
           strstr(config, "DESKTOP_THEME=light") != NULL;
}

static void copy_field(char *target, size_t capacity,
                       const char *begin, const char *end) {
    if (!target || capacity == 0U) return;
    size_t length = end > begin ? (size_t)(end - begin) : 0U;
    if (length >= capacity) length = capacity - 1U;
    if (length) memcpy(target, begin, length);
    target[length] = '\0';
}

static void launcher_load_registry(struct aurora_launcher *launcher) {
    char registry[2048];
    memset(launcher, 0, sizeof(*launcher));
    if (!read_text_file("/etc/aurora-apps", registry, sizeof(registry))) return;
    const char *line = registry;
    while (*line && launcher->app_count < AURORA_MAX_APPS) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        if (*line && *line != '#') {
            const char *first = strchr(line, '|');
            const char *second = first ? strchr(first + 1, '|') : NULL;
            const char *third = second ? strchr(second + 1, '|') : NULL;
            if (first && second && third && first < end && second < end && third < end) {
                struct aurora_app *app = &launcher->apps[launcher->app_count++];
                copy_field(app->id, sizeof(app->id), line, first);
                copy_field(app->name, sizeof(app->name), first + 1, second);
                copy_field(app->executable, sizeof(app->executable), second + 1, third);
                copy_field(app->icon, sizeof(app->icon), third + 1, end);
            }
        }
        line = *end ? end + 1 : end;
    }
}

static int ascii_contains_casefold(const char *text, const char *query) {
    if (!query || !*query) return 1;
    if (!text) return 0;
    for (size_t start = 0; text[start]; ++start) {
        size_t offset = 0;
        while (query[offset] && text[start + offset]) {
            char a = text[start + offset];
            char b = query[offset];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
            ++offset;
        }
        if (!query[offset]) return 1;
    }
    return 0;
}

static unsigned launcher_match_count(const struct aurora_launcher *launcher) {
    unsigned count = 0;
    for (unsigned i = 0; i < launcher->app_count; ++i)
        if (ascii_contains_casefold(launcher->apps[i].name, launcher->search)) ++count;
    return count;
}

static const struct aurora_app *launcher_match_at(
    const struct aurora_launcher *launcher, unsigned wanted) {
    unsigned found = 0;
    for (unsigned i = 0; i < launcher->app_count; ++i) {
        if (!ascii_contains_casefold(launcher->apps[i].name, launcher->search)) continue;
        if (found++ == wanted) return &launcher->apps[i];
    }
    return NULL;
}

static void capabilities_refresh(struct aurora_capabilities *caps) {
    char buffer[512];
    memset(caps, 0, sizeof(*caps));
    if (read_text_file("/proc/net/dev", buffer, sizeof(buffer)) &&
        strstr(buffer, "eth0:")) caps->network = 1;
    if (caps->network && read_text_file("/proc/net/route", buffer, sizeof(buffer)) &&
        strstr(buffer, "eth0")) caps->network_connected = 1;
    if (read_text_file("/proc/asound/cards", buffer, sizeof(buffer)) &&
        strchr(buffer, '[')) caps->audio = 1;
    /* XSCore does not expose a battery class yet. Absence is intentional:
     * never invent a percentage or reserve a placeholder in the tray. */
    caps->battery = 0;
}

static int leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int clock_refresh(struct aurora_clock *clock) {
    char text[32];
    if (!read_text_file("/proc/realtime", text, sizeof(text))) {
        clock->valid = 0;
        return 0;
    }
    uint32_t epoch = 0;
    size_t index = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        uint32_t digit = (uint32_t)(text[index++] - '0');
        if (epoch > (UINT32_MAX - digit) / 10U) {
            clock->valid = 0;
            return 0;
        }
        epoch = epoch * 10U + digit;
    }
    if (index == 0 || epoch == 0) {
        clock->valid = 0;
        return 0;
    }

    uint32_t days = epoch / 86400U;
    uint32_t remaining = epoch % 86400U;
    int year = 1970;
    for (;;) {
        uint32_t year_days = leap_year(year) ? 366U : 365U;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
    int month = 1;
    while (month <= 12) {
        uint32_t count = month == 2 && leap_year(year) ? 29U : month_days[month - 1];
        if (days < count) break;
        days -= count;
        ++month;
    }
    clock->year = year;
    clock->month = month;
    clock->day = (int)days + 1;
    clock->hour = (int)(remaining / 3600U);
    remaining %= 3600U;
    clock->minute = (int)(remaining / 60U);
    clock->second = (int)(remaining % 60U);
    clock->valid = 1;
    return 1;
}

static uint32_t alpha_blend(uint32_t background, uint32_t foreground,
                            uint8_t alpha) {
    uint32_t inverse = 255U - alpha;
    uint32_t r = (((background >> 16) & 0xffU) * inverse +
                  ((foreground >> 16) & 0xffU) * alpha + 127U) / 255U;
    uint32_t g = (((background >> 8) & 0xffU) * inverse +
                  ((foreground >> 8) & 0xffU) * alpha + 127U) / 255U;
    uint32_t b = ((background & 0xffU) * inverse +
                  (foreground & 0xffU) * alpha + 127U) / 255U;
    return (r << 16) | (g << 8) | b;
}

static void draw_ui_text(uint32_t *pixels, uint32_t width, uint32_t height,
                         int x, int y, int scale, const char *text,
                         uint32_t color, int max_width) {
    if (!pixels || !text || scale <= 0 || max_width <= 0) return;
    int origin_x = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (origin_x >= x + max_width) break;
        for (uint32_t row = 0; row < AKAOS_UI_FONT_HEIGHT; ++row) {
            for (uint32_t column = 0; column < AKAOS_UI_FONT_WIDTH; ++column) {
                uint8_t alpha = akaos_ui_font_get_alpha(text[index], row, column);
                if (!alpha) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    int py = y + (int)row * scale + sy;
                    if (py < 0 || py >= (int)height) continue;
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = origin_x + (int)column * scale + sx;
                        if (px >= 0 && px < (int)width && px < x + max_width) {
                            size_t offset = (size_t)py * width + (size_t)px;
                            pixels[offset] = alpha_blend(pixels[offset], color, alpha);
                        }
                    }
                }
            }
        }
        origin_x += (int)akaos_ui_font_get_advance(text[index]) * scale;
    }
}

static void fill_rounded_rect(uint32_t *pixels, uint32_t width, uint32_t height,
                              int x, int y, int rect_width, int rect_height,
                              int radius, uint32_t fill, uint32_t border) {
    if (!pixels || rect_width <= 0 || rect_height <= 0 || radius < 0) return;
    for (int py = y; py < y + rect_height; ++py) {
        if (py < 0 || py >= (int)height) continue;
        for (int px = x; px < x + rect_width; ++px) {
            if (px < 0 || px >= (int)width) continue;
            int local_x = px - x;
            int local_y = py - y;
            int corner_x = local_x < radius ? radius - local_x
                           : local_x >= rect_width - radius
                               ? local_x - (rect_width - radius - 1) : 0;
            int corner_y = local_y < radius ? radius - local_y
                           : local_y >= rect_height - radius
                               ? local_y - (rect_height - radius - 1) : 0;
            if (corner_x > 0 && corner_y > 0 &&
                corner_x * corner_x + corner_y * corner_y > radius * radius) continue;
            int edge = local_x == 0 || local_y == 0 ||
                       local_x == rect_width - 1 || local_y == rect_height - 1;
            pixels[(size_t)py * width + (size_t)px] = edge ? border : fill;
        }
    }
}

static void fill_rect(uint32_t *pixels, uint32_t width, uint32_t height,
                      int x, int y, int rect_width, int rect_height,
                      uint32_t color) {
    for (int py = y; py < y + rect_height; ++py) {
        if (py < 0 || py >= (int)height) continue;
        for (int px = x; px < x + rect_width; ++px) {
            if (px >= 0 && px < (int)width)
                pixels[(size_t)py * width + (size_t)px] = color;
        }
    }
}

static void draw_app_icon(uint32_t *pixels, uint32_t width, uint32_t height,
                          int x, int y, const char *identity) {
    fill_rounded_rect(pixels, width, height, x, y, 28, 28, 7,
                      UINT32_C(0x27364d), UINT32_C(0x465b78));
    if (identity && (strcmp(identity, "notepad") == 0 || strstr(identity, "Notepad"))) {
        fill_rounded_rect(pixels, width, height, x + 7, y + 5, 15, 18, 2,
                          UINT32_C(0xf4f7fb), UINT32_C(0xbcc9d9));
        fill_rect(pixels, width, height, x + 10, y + 10, 9, 2, UINT32_C(0x4e93e6));
        fill_rect(pixels, width, height, x + 10, y + 15, 9, 1, UINT32_C(0x8495a9));
        fill_rect(pixels, width, height, x + 10, y + 19, 7, 1, UINT32_C(0x8495a9));
    } else if (identity && (strcmp(identity, "taskmgr") == 0 ||
                            strstr(identity, "Task") || strstr(identity, "Manager"))) {
        fill_rect(pixels, width, height, x + 7, y + 16, 3, 7, UINT32_C(0x65b8ff));
        fill_rect(pixels, width, height, x + 12, y + 11, 3, 12, UINT32_C(0x75dfa9));
        fill_rect(pixels, width, height, x + 17, y + 7, 3, 16, UINT32_C(0xc08cff));
    } else if (identity && (strcmp(identity, "terminal") == 0 || strstr(identity, "Terminal"))) {
        fill_rounded_rect(pixels, width, height, x + 5, y + 6, 18, 16, 3,
                          UINT32_C(0x10141b), UINT32_C(0x75859b));
        draw_ui_text(pixels, width, height, x + 8, y + 5, 1, ">",
                     UINT32_C(0x76e0ac), 8);
    } else if (identity && (strcmp(identity, "browser") == 0 ||
                            strstr(identity, "Web") || strstr(identity, "Browser"))) {
        fill_rounded_rect(pixels, width, height, x + 6, y + 6, 17, 17, 8,
                          UINT32_C(0x4ba9ff), UINT32_C(0x8ac8ff));
        fill_rect(pixels, width, height, x + 8, y + 13, 13, 2, UINT32_C(0xe7f4ff));
        fill_rect(pixels, width, height, x + 13, y + 8, 2, 13, UINT32_C(0xe7f4ff));
    } else {
        fill_rect(pixels, width, height, x + 7, y + 7, 6, 6, UINT32_C(0x61b7ff));
        fill_rect(pixels, width, height, x + 15, y + 7, 6, 6, UINT32_C(0x79e0b2));
        fill_rect(pixels, width, height, x + 7, y + 15, 6, 6, UINT32_C(0xffca72));
        fill_rect(pixels, width, height, x + 15, y + 15, 6, 6, UINT32_C(0xc790ff));
    }
}

static void draw_wifi_icon(uint32_t *pixels, uint32_t width, uint32_t height,
                           int center_x, int center_y, int connected) {
    uint32_t color = connected
                         ? (g_light_theme ? UINT32_C(0x33455d) : UINT32_C(0xd9e4f2))
                         : UINT32_C(0x7d8999);
    for (int py = center_y - 11; py <= center_y + 2; ++py) {
        for (int px = center_x - 12; px <= center_x + 12; ++px) {
            int dx = px - center_x;
            int dy = py - (center_y + 3);
            int d2 = dx * dx + dy * dy;
            int upper = py <= center_y + 1;
            if (upper && ((d2 >= 125 && d2 <= 170) ||
                          (d2 >= 55 && d2 <= 85) ||
                          (d2 >= 13 && d2 <= 28))) {
                if (px >= 0 && px < (int)width && py >= 0 && py < (int)height)
                    pixels[(size_t)py * width + (size_t)px] = color;
            }
        }
    }
    fill_rounded_rect(pixels, width, height, center_x - 2, center_y, 5, 5, 2,
                      color, color);
    if (!connected) {
        for (int offset = -7; offset <= 7; ++offset)
            fill_rect(pixels, width, height, center_x + offset,
                      center_y - offset - 3, 2, 2, UINT32_C(0xe58b95));
    }
}

static void draw_audio_icon(uint32_t *pixels, uint32_t width, uint32_t height,
                            int center_x, int center_y) {
    uint32_t color = g_light_theme ? UINT32_C(0x33455d) : UINT32_C(0xd9e4f2);
    fill_rect(pixels, width, height, center_x - 9, center_y - 3, 5, 7, color);
    for (int row = -7; row <= 7; ++row) {
        int span = 7 - (row < 0 ? -row : row);
        if (span >= 2)
            fill_rect(pixels, width, height, center_x - 4, center_y + row,
                      span / 2 + 1, 1, color);
    }
    for (int row = -6; row <= 6; ++row) {
        int absolute = row < 0 ? -row : row;
        int offset = 7 - absolute / 2;
        if (absolute != 1 && absolute != 2)
            fill_rect(pixels, width, height, center_x + offset,
                      center_y + row, 2, 1, color);
    }
}

static uint32_t visible_window_count(const struct akaos_compositor_channel *channel,
                                     uint32_t limit) {
    if (!channel) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES && count < limit; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (slot->active &&
            !(slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL))) {
            ++count;
        }
    }
    return count;
}

static uint32_t panel_window_state(const struct akaos_compositor_channel *channel) {
    if (!channel) return 0;
    uint32_t state = channel->active_surface_id * UINT32_C(16777619);
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (!slot->active ||
            (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL))) continue;
        state ^= slot->surface_id + slot->flags * 31U + slot->z_order * 131U;
        state *= UINT32_C(16777619);
    }
    return state;
}

struct aurora_surface {
    int fd;
    size_t size;
    char name[32];
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static struct aurora_surface create_surface(const char *name, uint32_t width, uint32_t height) {
    struct aurora_surface surf;
    memset(&surf, 0, sizeof(surf));
    strncpy(surf.name, name, sizeof(surf.name) - 1);

    size_t content_size = sizeof(struct akaos_surface_header) +
                          (size_t)width * (size_t)height * sizeof(uint32_t);
    surf.size = (content_size + 4095U) & ~(size_t)4095U;

    surf.fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (surf.fd < 0 && errno == EEXIST) {
        (void)shm_unlink(name);
        surf.fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    }
    if (surf.fd < 0) return surf;

    if (ftruncate(surf.fd, (off_t)surf.size) != 0) {
        close(surf.fd);
        surf.fd = -1;
        return surf;
    }

    surf.header = (struct akaos_surface_header *)mmap(
        NULL, surf.size, PROT_READ | PROT_WRITE, MAP_SHARED, surf.fd, 0);
    if (surf.header == MAP_FAILED) {
        close(surf.fd);
        surf.fd = -1;
        surf.header = NULL;
        return surf;
    }

    surf.header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    surf.header->width = width;
    surf.header->height = height;
    surf.header->stride_pixels = width;
    surf.header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    surf.header->pixel_offset = sizeof(struct akaos_surface_header);
    surf.header->byte_size = surf.size;
    surf.pixels = (uint32_t *)((uint8_t *)surf.header + surf.header->pixel_offset);

    return surf;
}

static void destroy_surface(struct aurora_surface *surf) {
    if (!surf) return;
    if (surf->header && surf->header != MAP_FAILED) {
        munmap(surf->header, surf->size);
        surf->header = NULL;
    }
    if (surf->fd >= 0) {
        close(surf->fd);
        surf->fd = -1;
    }
    shm_unlink(surf->name);
}

static void render_launcher(uint32_t *pixels, uint32_t width, uint32_t height,
                            const struct aurora_launcher *launcher) {
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 14,
                      g_light_theme ? UINT32_C(0xf4f7fb) : UINT32_C(0x141c29),
                      g_light_theme ? UINT32_C(0xbac5d3) : UINT32_C(0x50617a));
    draw_ui_text(pixels, width, height, 18, 12, 1, "Applications",
                 g_light_theme ? UINT32_C(0x1c2a3e) : UINT32_C(0xf4f7fc),
                 (int)width - 36);
    fill_rounded_rect(pixels, width, height, 14, 38, (int)width - 28, 32, 8,
                      g_light_theme ? UINT32_C(0xffffff) : UINT32_C(0x202c3e),
                      g_light_theme ? UINT32_C(0xc3cedc) : UINT32_C(0x465873));
    /* Magnifier icon. */
    fill_rounded_rect(pixels, width, height, 24, 47, 11, 11, 5,
                      UINT32_C(0x202c3e), UINT32_C(0x9eacc0));
    fill_rect(pixels, width, height, 34, 57, 7, 2, UINT32_C(0x9eacc0));
    const char *query = launcher->search_length ? launcher->search : "Search applications";
    draw_ui_text(pixels, width, height, 48, 45, 1, query,
                 launcher->search_length
                     ? (g_light_theme ? UINT32_C(0x243348) : UINT32_C(0xf0f4fa))
                     : UINT32_C(0x748298),
                 (int)width - 64);

    unsigned matches = launcher_match_count(launcher);
    if (matches == 0U) {
        draw_ui_text(pixels, width, height, 20, 92, 1, "No matching applications",
                     UINT32_C(0x9eacc0), (int)width - 40);
        return;
    }
    unsigned visible = (height > 86U) ? (height - 86U) / 38U : 0U;
    if (visible > matches) visible = matches;
    unsigned first = launcher->selected >= visible ? launcher->selected - visible + 1U : 0U;
    for (unsigned row = 0; row < visible; ++row) {
        unsigned match_index = first + row;
        const struct aurora_app *app = launcher_match_at(launcher, match_index);
        if (!app) break;
        int y = 80 + (int)row * 38;
        if (match_index == launcher->selected)
            fill_rounded_rect(pixels, width, height, 12, y, (int)width - 24, 34, 8,
                              g_light_theme ? UINT32_C(0xdbe9fb) : UINT32_C(0x2b3e59),
                              g_light_theme ? UINT32_C(0x76a5da) : UINT32_C(0x4f79aa));
        draw_app_icon(pixels, width, height, 18, y + 2, app->icon);
        draw_ui_text(pixels, width, height, 55, y + 7, 1, app->name,
                     g_light_theme ? UINT32_C(0x26364b) : UINT32_C(0xeaf0f8),
                     (int)width - 68);
    }
}

static void launch_application(const struct aurora_app *app) {
    if (!app || app->executable[0] != '/') return;
    pid_t child = fork();
    if (child == 0) {
        char *arguments[] = {(char *)app->executable, NULL};
        execv(app->executable, arguments);
        _exit(127);
    }
}

static void render_context_menu(uint32_t *pixels, uint32_t width, uint32_t height) {
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 11,
                      g_light_theme ? UINT32_C(0xf5f7fa) : UINT32_C(0x17202e),
                      g_light_theme ? UINT32_C(0xb9c4d2) : UINT32_C(0x52627a));
    fill_rounded_rect(pixels, width, height, 8, 8, (int)width - 16, 30, 7,
                      UINT32_C(0x223047), UINT32_C(0x344a68));
    draw_app_icon(pixels, width, height, 12, 8, "terminal");
    draw_ui_text(pixels, width, height, 50, 13, 1, "Open Terminal",
                 g_light_theme ? UINT32_C(0x25354a) : UINT32_C(0xecf2fa),
                 (int)width - 62);
    fill_rounded_rect(pixels, width, height, 8, 44, (int)width - 16, 30, 7,
                      UINT32_C(0x1c283a), UINT32_C(0x2f4058));
    draw_ui_text(pixels, width, height, 18, 49, 1, "Show Applications",
                 g_light_theme ? UINT32_C(0x25354a) : UINT32_C(0xecf2fa),
                 (int)width - 36);
}

static int desktop_is_empty_at(const struct akaos_compositor_channel *channel,
                               int x, int y) {
    if (!channel) return 1;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (!slot->active || (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                              AKAOS_SURFACE_FLAG_PANEL |
                                              AKAOS_SURFACE_FLAG_MINIMIZED))) continue;
        if (x >= slot->x && x < slot->x + (int)slot->width &&
            y >= slot->y - 28 && y < slot->y + (int)slot->height) return 0;
    }
    return 1;
}

static void render_notification(uint32_t *pixels, uint32_t width, uint32_t height,
                                const struct akaos_notification *note,
                                const struct aurora_clock *clock) {
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 12,
                      g_light_theme ? UINT32_C(0xf8fafc) : UINT32_C(0x172130),
                      g_light_theme ? UINT32_C(0xb9c5d3) : UINT32_C(0x50627b));
    draw_app_icon(pixels, width, height, 14, 14,
                  note->app_id[0] ? note->app_id : "system");
    draw_ui_text(pixels, width, height, 54, 10, 1,
                 note->title[0] ? note->title : "akaOS",
                 g_light_theme ? UINT32_C(0x203047) : UINT32_C(0xf3f6fb),
                 (int)width - 105);
    if (clock && clock->valid) {
        char stamp[8];
        snprintf(stamp, sizeof(stamp), "%02d:%02d", clock->hour, clock->minute);
        draw_ui_text(pixels, width, height, (int)width - 48, 10, 1, stamp,
                     UINT32_C(0x78879b), 42);
    }
    draw_ui_text(pixels, width, height, 54, 38, 1, note->body,
                 g_light_theme ? UINT32_C(0x4b5a6e) : UINT32_C(0xb9c5d5),
                 (int)width - 68);
    draw_ui_text(pixels, width, height, (int)width - 22, 62, 1, "x",
                 UINT32_C(0x8795a8), 12);
}

static void render_panel(uint32_t *pixels, uint32_t width, uint32_t height,
                         struct akaos_compositor_channel *channel,
                         const struct aurora_clock *clock,
                         const struct aurora_capabilities *caps) {
    /* Full-width dark glass rail. */
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t fade = (y * 7U) / (height ? height : 1U);
        uint32_t panel_r = g_light_theme ? 0xf5U - fade : 0x16U - fade;
        uint32_t panel_g = g_light_theme ? 0xf7U - fade : 0x1dU - fade;
        uint32_t panel_b = g_light_theme ? 0xfaU - fade : 0x2cU - fade;
        uint32_t row_color = (panel_r << 16) | (panel_g << 8) | panel_b;
        if (y == 0) row_color = g_light_theme ? UINT32_C(0xbac5d3)
                                               : UINT32_C(0x31425d);
        for (uint32_t x = 0; x < width; ++x) {
            pixels[y * width + x] = row_color;
        }
    }

    uint32_t tray_icons = caps ? (uint32_t)(caps->network + caps->audio + caps->battery) : 0U;
    uint32_t clock_width = clock && clock->valid ? 112U : 0U;
    uint32_t tray_width = tray_icons * 32U + clock_width + 20U;
    if (tray_width < 20U) tray_width = 20U;
    uint32_t reserved = tray_width + 32U;
    uint32_t max_tasks = width > reserved + LAUNCHER_WIDTH
                             ? (width - reserved - LAUNCHER_WIDTH) / (TASK_WIDTH + TASK_GAP)
                             : 0U;
    uint32_t task_count = visible_window_count(channel, max_tasks);
    uint32_t dock_width = LAUNCHER_WIDTH +
                          (task_count ? 8U + task_count * (TASK_WIDTH + TASK_GAP) - TASK_GAP : 0U);
    uint32_t dock_x = width > dock_width ? (width - dock_width) / 2U : 4U;
    if (channel) {
        channel->shell_launcher_x = dock_x;
        channel->shell_task_count = task_count;
    }
    fill_rounded_rect(pixels, width, height, (int)dock_x, 6,
                      (int)LAUNCHER_WIDTH, (int)height - 12, 9,
                      g_light_theme ? UINT32_C(0xe3e9f1) : UINT32_C(0x25324a),
                      g_light_theme ? UINT32_C(0xb6c2d1) : UINT32_C(0x405270));

    /* Centered four-pane launcher, borrowing the clarity of modern desktop
     * docks without copying another operating system's artwork. */
    uint32_t mark_x = dock_x + 11U;
    uint32_t mark_y = 15U;
    uint32_t mark_colors[4] = {UINT32_C(0x61b7ff), UINT32_C(0x79e0b2),
                               UINT32_C(0xffca72), UINT32_C(0xc790ff)};
    for (uint32_t mark = 0; mark < 4; ++mark) {
        uint32_t mx = mark_x + (mark % 2U) * 11U;
        uint32_t my = mark_y + (mark / 2U) * 11U;
        for (uint32_t yy = 0; yy < 8; ++yy)
            for (uint32_t xx = 0; xx < 8; ++xx)
                pixels[(my + yy) * width + mx + xx] = mark_colors[mark];
    }

    /* Icon-only running applications, like a modern desktop dock. */
    if (channel) {
        uint32_t task_x = dock_x + LAUNCHER_WIDTH + 8U;
        uint32_t drawn = 0;
        for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
            const struct akaos_compositor_slot *s = &channel->surfaces[i];
            if (!s->active ||
                (s->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL)) ||
                drawn >= task_count) continue;
            int focused = s->surface_id == channel->active_surface_id;
            fill_rounded_rect(pixels, width, height, (int)task_x, 8,
                              (int)TASK_WIDTH, (int)height - 16, 8,
                              focused ? (g_light_theme ? UINT32_C(0xd5e5fa) : UINT32_C(0x31445f))
                                      : (g_light_theme ? UINT32_C(0xe9edf3) : UINT32_C(0x1c2635)),
                              focused ? UINT32_C(0x628fd0)
                                      : (g_light_theme ? UINT32_C(0xc3ccd8) : UINT32_C(0x303f54)));
            draw_app_icon(pixels, width, height, (int)task_x + 7, 10,
                          s->icon_name[0] ? s->icon_name :
                          (s->title[0] ? s->title : "Window"));
            if (focused) {
                fill_rounded_rect(pixels, width, height, (int)task_x + 15,
                                  (int)height - 5, 12, 3, 1,
                                  UINT32_C(0x7eb1ff), UINT32_C(0x7eb1ff));
            }
            task_x += TASK_WIDTH + TASK_GAP;
            ++drawn;
        }
    }

    /* Capability-driven tray. Missing hardware gets neither an icon nor a
     * placeholder. Clock data comes from the kernel RTC interface. */
    if (width > tray_width + 16U && tray_width > 20U) {
        uint32_t status_x = width - tray_width - 16U;
        fill_rounded_rect(pixels, width, height, (int)status_x, 7,
                          (int)tray_width, (int)height - 14, 9,
                          g_light_theme ? UINT32_C(0xe7ecf3) : UINT32_C(0x1c2635),
                          g_light_theme ? UINT32_C(0xbec8d5) : UINT32_C(0x303f54));
        uint32_t cursor_x = status_x + 18U;
        if (caps && caps->network) {
            draw_wifi_icon(pixels, width, height, (int)cursor_x, 28,
                           caps->network_connected);
            cursor_x += 32U;
        }
        if (caps && caps->audio) {
            draw_audio_icon(pixels, width, height, (int)cursor_x, 24);
            cursor_x += 32U;
        }
        if (clock && clock->valid) {
            if (cursor_x > status_x + 18U) {
                fill_rect(pixels, width, height, (int)cursor_x, 14, 1, 20,
                          UINT32_C(0x3b4a60));
                cursor_x += 13U;
            }
            char time_text[8];
            char date_text[12];
            snprintf(time_text, sizeof(time_text), "%02d:%02d", clock->hour, clock->minute);
            snprintf(date_text, sizeof(date_text), "%02d/%02d/%02d",
                     clock->month, clock->day, clock->year % 100);
            draw_ui_text(pixels, width, height, (int)cursor_x, 6, 1,
                         time_text,
                         g_light_theme ? UINT32_C(0x243348) : UINT32_C(0xf2f5fa), 58);
            draw_ui_text(pixels, width, height, (int)cursor_x, 24, 1,
                         date_text,
                         g_light_theme ? UINT32_C(0x66758a) : UINT32_C(0xaebbd0), 104);
        }
    }
}

static uint32_t panel_launcher_x(uint32_t width,
                                 const struct akaos_compositor_channel *channel,
                                 const struct aurora_clock *clock,
                                 const struct aurora_capabilities *caps) {
    uint32_t tray_icons = caps ? (uint32_t)(caps->network + caps->audio + caps->battery) : 0U;
    uint32_t tray_width = tray_icons * 32U +
                          (clock && clock->valid ? 112U : 0U) + 20U;
    uint32_t reserved = tray_width + 32U;
    uint32_t max_tasks = width > reserved + LAUNCHER_WIDTH
                             ? (width - reserved - LAUNCHER_WIDTH) / (TASK_WIDTH + TASK_GAP)
                             : 0U;
    uint32_t task_count = visible_window_count(channel, max_tasks);
    uint32_t dock_width = LAUNCHER_WIDTH +
                          (task_count ? 8U + task_count * (TASK_WIDTH + TASK_GAP) - TASK_GAP : 0U);
    return width > dock_width ? (width - dock_width) / 2U : 4U;
}

static int register_slot(struct akaos_compositor_channel *channel,
                         uint32_t surface_id, int32_t x, int32_t y,
                         uint32_t width, uint32_t height, uint32_t flags,
                         uint32_t z_order, const char *shm_name, const char *title) {
    if (!channel || channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION ||
        !shm_name || !title) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_ATTACH;
    message.flags = flags;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.z_order = z_order;
    message.damage_w = width;
    message.damage_h = height;
    strncpy(message.shm_name, shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, title, sizeof(message.title) - 1);
    if (strcmp(title, "Desktop Panel") == 0) strncpy(message.icon_name, "panel", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Applications") == 0) strncpy(message.icon_name, "launcher", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Desktop Menu") == 0) strncpy(message.icon_name, "menu", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int queue_damage(struct akaos_compositor_channel *channel,
                        uint32_t surface_id, uint32_t width, uint32_t height) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.damage_w = width;
    message.damage_h = height;
    return akaos_compositor_submit(channel, &message);
}

static int queue_destroy(struct akaos_compositor_channel *channel, uint32_t surface_id) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DESTROY;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    return akaos_compositor_submit(channel, &message);
}

int main(int argc, char **argv) {
    g_light_theme = load_light_theme();
    int once = 0;
    int spawn_app = 0;
    int request_launcher = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            puts("Usage: aurora [options]\n"
                 "akaOS Desktop Environment Shell and Panel\n\n"
                 "Options:\n"
                 "  --once           Initialize once and exit (for self-testing)\n"
                 "  --spawn-palette  Spawn palette application\n"
                 "  --show-launcher  Ask the running shell to open its launcher\n"
                 "  -v, --version    Output version information and exit\n"
                 "  -h, --help       Display this help and exit");
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            puts("aurora (akaOS Desktop Environment) 2.0");
            return 0;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--spawn-palette") == 0) {
            spawn_app = 1;
        } else if (strcmp(argv[i], "--show-launcher") == 0) {
            request_launcher = 1;
        }
    }

    int info_fd = open(AKAOS_FRAMEBUFFER_PATH, O_RDONLY);
    if (info_fd < 0) {
        printf("aurora: no active framebuffer: %d\n", errno);
        return 1;
    }
    struct akaos_display_info display;
    if (read(info_fd, &display, sizeof(display)) != (ssize_t)sizeof(display) ||
        close(info_fd) != 0 || display.abi_version != AKAOS_DISPLAY_ABI_VERSION) {
        puts("aurora: invalid display info");
        return 2;
    }

    /* Open compositor control channel */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd < 0) {
        if (once) {
            printf("aurora: desktop shell initialized\n");
            return 0;
        }
        printf("aurora: cannot connect to compositor at %s (%d)\n",
               AKAOS_COMPOSITOR_IPC_PATH, errno);
        return 3;
    }

    size_t ipc_size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
    struct akaos_compositor_channel *channel = mmap(
        NULL, ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
    close(ipc_fd);
    if (channel == MAP_FAILED || channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
        puts("aurora: failed to map compositor channel");
        if (channel != MAP_FAILED) munmap(channel, ipc_size);
        return 4;
    }

    if (request_launcher) {
        struct akaos_shell_event event;
        memset(&event, 0, sizeof(event));
        event.type = AKAOS_SHELL_EVENT_KEY;
        event.code = 59;
        event.value = 1;
        int submitted = akaos_shell_event_submit(channel, &event);
        munmap(channel, ipc_size);
        return submitted ? 0 : 1;
    }

    pid_t my_pid = getpid();
    char bar_name[32];
    snprintf(bar_name, sizeof(bar_name), "/aurora-bar-%d", (int)my_pid);

    struct aurora_surface bar = create_surface(bar_name, display.width, PANEL_HEIGHT);
    if (!bar.header) {
        puts("aurora: failed to create desktop surfaces");
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 5;
    }

    /* Lumen owns the full-screen wallpaper so Aurora does not need a
     * multi-megabyte shared-memory buffer.  This keeps the desktop portable
     * on every XSCore architecture while the shell owns the live dock. */
    struct aurora_clock panel_clock;
    memset(&panel_clock, 0, sizeof(panel_clock));
    (void)clock_refresh(&panel_clock);
    struct aurora_capabilities capabilities;
    capabilities_refresh(&capabilities);
    struct aurora_launcher launcher;
    launcher_load_registry(&launcher);
    render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel, &panel_clock,
                 &capabilities);

    int32_t bar_y = (int32_t)(display.height - PANEL_HEIGHT);

    uint32_t bar_surface_id = (uint32_t)(my_pid * 10 + 2);
    uint32_t launcher_surface_id = (uint32_t)(my_pid * 10 + 3);
    uint32_t context_surface_id = (uint32_t)(my_pid * 10 + 4);
    uint32_t notification_surface_id = (uint32_t)(my_pid * 10 + 5);
    int bar_slot = register_slot(channel, bar_surface_id, 0,
                                 bar_y,
                                 display.width, PANEL_HEIGHT,
                                 AKAOS_SURFACE_FLAG_PANEL, 1000,
                                 bar_name, "Desktop Panel");

    if (bar_slot == 0) {
        puts("aurora: failed to register desktop slots with compositor");
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 6;
    }

#if defined(__i386__)
    uint32_t launcher_width = display.width > 340U ? 300U : display.width - 24U;
    uint32_t launcher_height = display.height > 250U ? 180U : display.height - PANEL_HEIGHT - 12U;
#else
    uint32_t launcher_width = display.width > 400U ? 360U : display.width - 24U;
    uint32_t launcher_height = display.height > 350U ? 260U : display.height - PANEL_HEIGHT - 12U;
#endif
    int32_t launcher_x = (int32_t)((display.width - launcher_width) / 2U);
    int32_t launcher_y = (int32_t)(display.height - PANEL_HEIGHT - launcher_height - 10U);
    char launcher_name[32];
    snprintf(launcher_name, sizeof(launcher_name), "/aurora-menu-%d", (int)my_pid);
    struct aurora_surface launcher_surface;
    memset(&launcher_surface, 0, sizeof(launcher_surface));
    launcher_surface.fd = -1;

    char context_name[32];
    snprintf(context_name, sizeof(context_name), "/aurora-menu2-%d", (int)my_pid);
    const uint32_t context_width = 210U;
    const uint32_t context_height = 82U;
    struct aurora_surface context_surface;
    memset(&context_surface, 0, sizeof(context_surface));
    context_surface.fd = -1;
    int32_t context_x = 0;
    int32_t context_y = 0;
    int context_open = 0;

#if defined(__i386__)
    const uint32_t notification_width = 280U;
#else
    const uint32_t notification_width = 320U;
#endif
    const uint32_t notification_height = 84U;
    char notification_name[32];
    snprintf(notification_name, sizeof(notification_name), "/aurora-note-%d", (int)my_pid);
    struct aurora_surface notification_surface;
    memset(&notification_surface, 0, sizeof(notification_surface));
    notification_surface.fd = -1;
    struct akaos_notification active_notification;
    memset(&active_notification, 0, sizeof(active_notification));
    int notification_active = 0;
    int notification_attached = 0;
    unsigned notification_ticks = 0;
    int32_t notification_x = (int32_t)display.width -
                             (int32_t)notification_width - 16;
    int32_t notification_y = 16;

    if (spawn_app) {
        pid_t app_pid = fork();
        if (app_pid == 0) {
            char *app_argv[] = {"/bin/palette", NULL};
            execv("/bin/palette", app_argv);
            _exit(1);
        }
    }

    if (once) {
        printf("aurora: desktop shell initialized\n");
        (void)queue_destroy(channel, bar_surface_id);
        (void)poll(NULL, 0, 50);
        destroy_surface(&notification_surface);
        destroy_surface(&context_surface);
        destroy_surface(&launcher_surface);
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 0;
    }

    /* Refresh the RTC once per second and the small capability files every
     * five seconds. Damage is no longer submitted in an unconditional 20 Hz
     * redraw loop. */
    unsigned ticks = 0;
    int launcher_attached = 0;
    int context_attached = 0;
    uint32_t observed_window_state = panel_window_state(channel);
    for (;;) {
        (void)poll(NULL, 0, 50);
        ++ticks;
        int launcher_changed = 0;
        int context_changed = 0;
        int notification_changed = 0;
        struct akaos_shell_event event;
        while (akaos_shell_event_receive(channel, &event)) {
            if (event.type == AKAOS_SHELL_EVENT_KEY &&
                (event.code == 125 || event.code == 59) &&
                event.value == 1) {
                launcher.open = !launcher.open;
                if (launcher.open) {
                    launcher.search_length = 0;
                    launcher.search[0] = '\0';
                    launcher.selected = 0;
                }
                launcher_changed = 1;
                if (context_open) {
                    context_open = 0;
                    context_changed = 1;
                }
                continue;
            }
            if (notification_active && event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_LEFT && event.value == 1 &&
                event.pointer_x >= notification_x &&
                event.pointer_x < notification_x + (int32_t)notification_width &&
                event.pointer_y >= notification_y &&
                event.pointer_y < notification_y + (int32_t)notification_height) {
                notification_active = 0;
                notification_changed = 1;
                continue;
            }
            if (event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_RIGHT && event.value == 1 &&
                event.pointer_y < (int32_t)(display.height - PANEL_HEIGHT) &&
                desktop_is_empty_at(channel, event.pointer_x, event.pointer_y)) {
                context_x = event.pointer_x;
                context_y = event.pointer_y;
                if (context_x + (int32_t)context_width > (int32_t)display.width)
                    context_x = (int32_t)display.width - (int32_t)context_width - 4;
                if (context_y + (int32_t)context_height >
                    (int32_t)display.height - (int32_t)PANEL_HEIGHT)
                    context_y = (int32_t)display.height - (int32_t)PANEL_HEIGHT -
                                (int32_t)context_height - 4;
                if (context_x < 4) context_x = 4;
                if (context_y < 4) context_y = 4;
                context_open = 1;
                context_changed = 1;
                if (launcher.open) {
                    launcher.open = 0;
                    launcher_changed = 1;
                }
                continue;
            }
            if (event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_LEFT && event.value == 1) {
                if (context_open) {
                    if (event.pointer_x >= context_x &&
                        event.pointer_x < context_x + (int32_t)context_width &&
                        event.pointer_y >= context_y &&
                        event.pointer_y < context_y + (int32_t)context_height) {
                        int local_y = event.pointer_y - context_y;
                        if (local_y >= 8 && local_y < 38) {
                            struct aurora_app terminal;
                            memset(&terminal, 0, sizeof(terminal));
                            strcpy(terminal.executable, "/bin/terminal");
                            launch_application(&terminal);
                        } else if (local_y >= 44 && local_y < 74) {
                            launcher.open = 1;
                            launcher.search_length = 0;
                            launcher.search[0] = '\0';
                            launcher.selected = 0;
                            launcher_changed = 1;
                        }
                    }
                    context_open = 0;
                    context_changed = 1;
                    continue;
                }
                uint32_t button_x = panel_launcher_x(display.width, channel,
                                                     &panel_clock, &capabilities);
                if (event.pointer_y >= (int32_t)(display.height - PANEL_HEIGHT) &&
                    event.pointer_x >= (int32_t)button_x &&
                    event.pointer_x < (int32_t)(button_x + LAUNCHER_WIDTH)) {
                    launcher.open = !launcher.open;
                    if (launcher.open) {
                        launcher.search_length = 0;
                        launcher.search[0] = '\0';
                        launcher.selected = 0;
                    }
                    launcher_changed = 1;
                } else if (launcher.open && event.pointer_x >= launcher_x &&
                           event.pointer_x < launcher_x + (int32_t)launcher_width &&
                           event.pointer_y >= launcher_y &&
                           event.pointer_y < launcher_y + (int32_t)launcher_height) {
                    int local_y = event.pointer_y - launcher_y;
                    if (local_y >= 80) {
                        unsigned visible = launcher_match_count(&launcher);
                        unsigned capacity = launcher_height > 86U
                                                ? (launcher_height - 86U) / 38U : 0U;
                        if (capacity > visible) capacity = visible;
                        unsigned first = launcher.selected >= capacity
                                             ? launcher.selected - capacity + 1U : 0U;
                        unsigned picked = first + (unsigned)(local_y - 80) / 38U;
                        const struct aurora_app *app = launcher_match_at(&launcher, picked);
                        if (app) {
                            launch_application(app);
                            launcher.open = 0;
                            launcher_changed = 1;
                        }
                    }
                } else if (launcher.open) {
                    launcher.open = 0;
                    launcher_changed = 1;
                }
            } else if (launcher.open && event.type == AKAOS_SHELL_EVENT_TEXT &&
                       event.value >= 32 && event.value <= 126 &&
                       launcher.search_length < AURORA_SEARCH_MAX) {
                launcher.search[launcher.search_length++] = (char)event.value;
                launcher.search[launcher.search_length] = '\0';
                launcher.selected = 0;
                launcher_changed = 1;
            } else if (launcher.open && event.type == AKAOS_SHELL_EVENT_KEY &&
                       event.value == 1) {
                unsigned matches = launcher_match_count(&launcher);
                if (event.code == 1) {
                    launcher.open = 0;
                    launcher_changed = 1;
                } else if (event.code == 14 && launcher.search_length) {
                    launcher.search[--launcher.search_length] = '\0';
                    launcher.selected = 0;
                    launcher_changed = 1;
                } else if (event.code == 103 && launcher.selected > 0) {
                    --launcher.selected;
                    launcher_changed = 1;
                } else if (event.code == 108 && launcher.selected + 1U < matches) {
                    ++launcher.selected;
                    launcher_changed = 1;
                } else if (event.code == 28) {
                    const struct aurora_app *app = launcher_match_at(&launcher,
                                                                     launcher.selected);
                    if (app) launch_application(app);
                    launcher.open = 0;
                    launcher_changed = 1;
                }
            }
        }
        if (!notification_active &&
            akaos_notification_receive(channel, &active_notification)) {
            notification_active = 1;
            notification_ticks = 0;
            notification_changed = 1;
        }
        if (notification_active) {
            ++notification_ticks;
            unsigned timeout_ticks = active_notification.timeout_ms / 50U;
            if (timeout_ticks < 20U) timeout_ticks = 20U;
            if (notification_ticks >= timeout_ticks) {
                notification_active = 0;
                notification_changed = 1;
            }
        }
        if (notification_changed) {
            if (notification_active) {
                if (!notification_surface.header) {
                    notification_surface = create_surface(notification_name,
                                                          notification_width,
                                                          notification_height);
                    if (!notification_surface.header) {
                        notification_active = 0;
                        continue;
                    }
                }
                render_notification(notification_surface.pixels,
                                    notification_width, notification_height,
                                    &active_notification, &panel_clock);
                if (!notification_attached) {
                    (void)register_slot(channel, notification_surface_id,
                                        notification_x, notification_y,
                                        notification_width, notification_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 1100,
                                        notification_name, "Notification");
                    notification_attached = 1;
                }
                (void)queue_damage(channel, notification_surface_id,
                                   notification_width, notification_height);
            } else if (notification_attached) {
                (void)queue_destroy(channel, notification_surface_id);
                notification_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&notification_surface);
            }
        }
        if (context_changed) {
            if (context_open) {
                if (!context_surface.header) {
                    context_surface = create_surface(context_name, context_width,
                                                     context_height);
                    if (!context_surface.header) {
                        context_open = 0;
                        continue;
                    }
                    render_context_menu(context_surface.pixels,
                                        context_width, context_height);
                }
                if (!context_attached) {
                    (void)register_slot(channel, context_surface_id, context_x, context_y,
                                        context_width, context_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 995,
                                        context_name, "Desktop Menu");
                    context_attached = 1;
                } else {
                    struct akaos_display_message move;
                    memset(&move, 0, sizeof(move));
                    move.type = AKAOS_DISPLAY_MSG_MOVE;
                    move.surface_id = context_surface_id;
                    move.client_pid = (uint32_t)my_pid;
                    move.x = context_x;
                    move.y = context_y;
                    (void)akaos_compositor_submit(channel, &move);
                }
                (void)queue_damage(channel, context_surface_id,
                                   context_width, context_height);
            } else if (context_attached) {
                (void)queue_destroy(channel, context_surface_id);
                context_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&context_surface);
            }
        }
        if (launcher_changed) {
            if (launcher.open) {
                if (!launcher_surface.header) {
                    launcher_surface = create_surface(launcher_name, launcher_width,
                                                      launcher_height);
                    if (!launcher_surface.header) {
                        launcher.open = 0;
                        channel->shell_flags &= ~AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
                        continue;
                    }
                }
                render_launcher(launcher_surface.pixels, launcher_width,
                                launcher_height, &launcher);
                if (!launcher_attached) {
                    (void)register_slot(channel, launcher_surface_id, launcher_x, launcher_y,
                                        launcher_width, launcher_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 990,
                                        launcher_name, "Applications");
                    launcher_attached = 1;
                }
                (void)queue_damage(channel, launcher_surface_id,
                                   launcher_width, launcher_height);
                channel->shell_flags |= AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
            } else {
                if (launcher_attached) {
                    (void)queue_destroy(channel, launcher_surface_id);
                    launcher_attached = 0;
                    (void)poll(NULL, 0, 60);
                    destroy_surface(&launcher_surface);
                }
                channel->shell_flags &= ~AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
            }
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }
        uint32_t current_window_state = panel_window_state(channel);
        if (current_window_state != observed_window_state) {
            observed_window_state = current_window_state;
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }
        if (ticks % 20U == 0U) {
            (void)clock_refresh(&panel_clock);
            if (ticks % 100U == 0U) capabilities_refresh(&capabilities);
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }
    }

    (void)queue_destroy(channel, bar_surface_id);
    (void)poll(NULL, 0, 50);
    destroy_surface(&launcher_surface);
    destroy_surface(&context_surface);
    destroy_surface(&notification_surface);
    destroy_surface(&bar);
    munmap(channel, ipc_size);
    return 0;
}
