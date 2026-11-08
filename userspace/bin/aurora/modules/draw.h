#ifndef AURORA_DRAW_H
#define AURORA_DRAW_H

#include "aurora_types.h"
#include <akaos/ui_font_aa.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static inline uint32_t alpha_blend(uint32_t background, uint32_t foreground, uint8_t alpha) {
    uint32_t inverse = 255U - alpha;
    uint32_t r = (((background >> 16) & 0xffU) * inverse +
                  ((foreground >> 16) & 0xffU) * alpha + 127U) / 255U;
    uint32_t g = (((background >> 8) & 0xffU) * inverse +
                  ((foreground >> 8) & 0xffU) * alpha + 127U) / 255U;
    uint32_t b = ((background & 0xffU) * inverse +
                  (foreground & 0xffU) * alpha + 127U) / 255U;
    return (r << 16) | (g << 8) | b;
}

static inline void draw_ui_text(uint32_t *pixels, uint32_t width, uint32_t height,
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

static inline void fill_rect(uint32_t *pixels, uint32_t width, uint32_t height,
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

static inline void fill_rounded_rect(uint32_t *pixels, uint32_t width, uint32_t height,
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

static inline void draw_app_icon(uint32_t *pixels, uint32_t width, uint32_t height,
                                 int x, int y, const char *identity) {
    fill_rounded_rect(pixels, width, height, x, y, 28, 28, 7,
                      UINT32_C(0x27364d), UINT32_C(0x465b78));
    if (identity && (strcmp(identity, "notepad") == 0 || strstr(identity, "Notepad") || strcmp(identity, "ted") == 0)) {
        fill_rounded_rect(pixels, width, height, x + 7, y + 5, 15, 18, 2,
                          UINT32_C(0xf4f7fb), UINT32_C(0xbcc9d9));
        fill_rect(pixels, width, height, x + 10, y + 10, 9, 2, UINT32_C(0x4e93e6));
        fill_rect(pixels, width, height, x + 10, y + 15, 9, 1, UINT32_C(0x8495a9));
        fill_rect(pixels, width, height, x + 10, y + 19, 7, 1, UINT32_C(0x8495a9));
    } else if (identity && (strcmp(identity, "taskmgr") == 0 || strcmp(identity, "sysmon") == 0 ||
                            strstr(identity, "Task") || strstr(identity, "Manager"))) {
        fill_rect(pixels, width, height, x + 7, y + 16, 3, 7, UINT32_C(0x65b8ff));
        fill_rect(pixels, width, height, x + 12, y + 11, 3, 12, UINT32_C(0x75dfa9));
        fill_rect(pixels, width, height, x + 17, y + 7, 3, 16, UINT32_C(0xc08cff));
    } else if (identity && (strcmp(identity, "terminal") == 0 || strstr(identity, "Terminal"))) {
        fill_rounded_rect(pixels, width, height, x + 5, y + 6, 18, 16, 3,
                          UINT32_C(0x10141b), UINT32_C(0x75859b));
        draw_ui_text(pixels, width, height, x + 8, y + 5, 1, ">",
                     UINT32_C(0x76e0ac), 8);
    } else if (identity && (strcmp(identity, "browser") == 0 || strcmp(identity, "webviewer") == 0 ||
                            strstr(identity, "Web") || strstr(identity, "Browser"))) {
        fill_rounded_rect(pixels, width, height, x + 6, y + 6, 17, 17, 8,
                          UINT32_C(0x4ba9ff), UINT32_C(0x8ac8ff));
        fill_rect(pixels, width, height, x + 8, y + 13, 13, 2, UINT32_C(0xe7f4ff));
        fill_rect(pixels, width, height, x + 13, y + 8, 2, 13, UINT32_C(0xe7f4ff));
    } else if (identity && (strcmp(identity, "akaos-config") == 0 || strcmp(identity, "akaosconfig") == 0 ||
                            strstr(identity, "Settings") || strstr(identity, "Config"))) {
        /* Gear icon for settings */
        fill_rounded_rect(pixels, width, height, x + 6, y + 6, 16, 16, 4,
                          UINT32_C(0x3a4b66), UINT32_C(0x7d94b8));
        fill_rounded_rect(pixels, width, height, x + 10, y + 10, 8, 8, 4,
                          UINT32_C(0x27364d), UINT32_C(0x7d94b8));
    } else {
        fill_rect(pixels, width, height, x + 7, y + 7, 6, 6, UINT32_C(0x61b7ff));
        fill_rect(pixels, width, height, x + 15, y + 7, 6, 6, UINT32_C(0x79e0b2));
        fill_rect(pixels, width, height, x + 7, y + 15, 6, 6, UINT32_C(0xffca72));
        fill_rect(pixels, width, height, x + 15, y + 15, 6, 6, UINT32_C(0xc790ff));
    }
}

static inline void draw_wifi_icon(uint32_t *pixels, uint32_t width, uint32_t height,
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

static inline void draw_audio_icon(uint32_t *pixels, uint32_t width, uint32_t height,
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

static inline struct aurora_surface create_surface(const char *name, uint32_t width, uint32_t height) {
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

static inline void destroy_surface(struct aurora_surface *surf) {
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

#endif /* AURORA_DRAW_H */
