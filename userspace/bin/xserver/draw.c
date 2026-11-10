/* Drawable primitives for the akaOS X11 display server. */
#include <stdlib.h>
#include <string.h>

#include "server.h"

uint32_t *x_drawable_pixels(struct x_server *server, xid_t drawable,
                            uint16_t *width, uint16_t *height, int *is_window) {
    int index = x_window_index(server, drawable);
    if (index >= 0) {
        struct x_window *window = &server->windows[index];
        if (width) *width = window->width;
        if (height) *height = window->height;
        if (is_window) *is_window = 1;
        return window->backing;
    }
    for (int slot = 0; slot < X_MAX_PIXMAPS; ++slot) {
        if (server->pixmaps[slot].id != drawable) continue;
        if (width) *width = server->pixmaps[slot].width;
        if (height) *height = server->pixmaps[slot].height;
        if (is_window) *is_window = 0;
        return server->pixmaps[slot].pixels;
    }
    return NULL;
}

void x_drawable_put_pixel(struct x_server *server, xid_t drawable, int16_t x,
                          int16_t y, uint32_t pixel) {
    uint16_t width = 0U;
    uint16_t height = 0U;
    int is_window = 0;
    uint32_t *pixels = x_drawable_pixels(server, drawable, &width, &height,
                                         &is_window);
    if (!pixels) return;
    if (x < 0 || y < 0 || x >= (int16_t)width || y >= (int16_t)height) return;
    pixels[(size_t)y * width + (size_t)x] = pixel;
}

void x_drawable_fill(struct x_server *server, xid_t drawable, int16_t x,
                     int16_t y, uint16_t width, uint16_t height, uint32_t pixel) {
    uint16_t drawable_width = 0U;
    uint16_t drawable_height = 0U;
    int is_window = 0;
    uint32_t *pixels = x_drawable_pixels(server, drawable, &drawable_width,
                                         &drawable_height, &is_window);
    if (!pixels) return;
    for (uint32_t row = 0U; row < height; ++row) {
        int32_t target_y = y + (int32_t)row;
        if (target_y < 0 || target_y >= (int32_t)drawable_height) continue;
        for (uint32_t column = 0U; column < width; ++column) {
            int32_t target_x = x + (int32_t)column;
            if (target_x < 0 || target_x >= (int32_t)drawable_width) continue;
            pixels[(size_t)target_y * drawable_width + target_x] = pixel;
        }
    }
}

void x_drawable_copy(struct x_server *server, xid_t source, int16_t src_x,
                     int16_t src_y, xid_t destination, int16_t dst_x,
                     int16_t dst_y, uint16_t width, uint16_t height) {
    uint16_t source_width = 0U;
    uint16_t source_height = 0U;
    int source_is_window = 0;
    uint32_t *source_pixels = x_drawable_pixels(server, source, &source_width,
                                                &source_height, &source_is_window);
    uint16_t destination_width = 0U;
    uint16_t destination_height = 0U;
    int destination_is_window = 0;
    uint32_t *destination_pixels =
        x_drawable_pixels(server, destination, &destination_width,
                          &destination_height, &destination_is_window);
    if (!source_pixels || !destination_pixels) return;

    /* Copy through a temporary so overlapping regions behave correctly. */
    size_t count = (size_t)width * (size_t)height;
    if (count == 0U) return;
    uint32_t *scratch = malloc(count * sizeof(uint32_t));
    if (!scratch) return;
    for (uint32_t row = 0U; row < height; ++row) {
        for (uint32_t column = 0U; column < width; ++column) {
            int32_t sx = src_x + (int32_t)column;
            int32_t sy = src_y + (int32_t)row;
            uint32_t value = 0U;
            if (sx >= 0 && sy >= 0 && sx < (int32_t)source_width &&
                sy < (int32_t)source_height) {
                value = source_pixels[(size_t)sy * source_width + sx];
            }
            scratch[(size_t)row * width + column] = value;
        }
    }
    for (uint32_t row = 0U; row < height; ++row) {
        int32_t ty = dst_y + (int32_t)row;
        if (ty < 0 || ty >= (int32_t)destination_height) continue;
        for (uint32_t column = 0U; column < width; ++column) {
            int32_t tx = dst_x + (int32_t)column;
            if (tx < 0 || tx >= (int32_t)destination_width) continue;
            destination_pixels[(size_t)ty * destination_width + tx] =
                scratch[(size_t)row * width + column];
        }
    }
    free(scratch);
}

void x_drawable_image(struct x_server *server, xid_t drawable, int16_t x,
                      int16_t y, uint16_t width, uint16_t height,
                      uint16_t depth, uint16_t format, const void *pixels,
                      size_t pixels_length) {
    uint16_t drawable_width = 0U;
    uint16_t drawable_height = 0U;
    int is_window = 0;
    uint32_t *target = x_drawable_pixels(server, drawable, &drawable_width,
                                         &drawable_height, &is_window);
    if (!target || !pixels) return;
    const uint8_t *source = pixels;

    if (format == 2U) {
        /* ZPixmap: 32 bits per pixel, native byte order. */
        for (uint32_t row = 0U; row < height; ++row) {
            for (uint32_t column = 0U; column < width; ++column) {
                int32_t tx = x + (int32_t)column;
                int32_t ty = y + (int32_t)row;
                if (tx < 0 || ty < 0 || tx >= (int32_t)drawable_width ||
                    ty >= (int32_t)drawable_height) {
                    continue;
                }
                uint32_t value;
                size_t offset = ((size_t)row * width + column) * 4U;
                if (offset > pixels_length ||
                    pixels_length - offset < sizeof(value)) return;
                memcpy(&value, source + offset, sizeof(value));
                target[(size_t)ty * drawable_width + tx] = value;
            }
        }
        return;
    }
    if (format == 1U) {
        /* XYPixmap: one plane, 32 bits of padding per scanline. */
        size_t stride = ((size_t)width + 31U) / 32U * 4U;
        for (uint32_t row = 0U; row < height; ++row) {
            for (uint32_t column = 0U; column < width; ++column) {
                int32_t tx = x + (int32_t)column;
                int32_t ty = y + (int32_t)row;
                if (tx < 0 || ty < 0 || tx >= (int32_t)drawable_width ||
                    ty >= (int32_t)drawable_height) {
                    continue;
                }
                size_t offset = (size_t)row * stride + (column / 8U);
                uint32_t value = 0U;
                if (offset < pixels_length) {
                    value = source[offset];
                }
                target[(size_t)ty * drawable_width + tx] = value ? 0xffffffffU : 0U;
            }
        }
        return;
    }
    /* XYBitmap: one bit per pixel, padded to 32 bits per scanline. */
    size_t stride = ((size_t)width + 31U) / 32U * 4U;
    for (uint32_t row = 0U; row < height; ++row) {
        for (uint32_t column = 0U; column < width; ++column) {
            int32_t tx = x + (int32_t)column;
            int32_t ty = y + (int32_t)row;
            if (tx < 0 || ty < 0 || tx >= (int32_t)drawable_width ||
                ty >= (int32_t)drawable_height) {
                continue;
            }
            size_t offset = (size_t)row * stride + (column / 8U);
            uint32_t bit = 0U;
            if (offset < pixels_length) {
                bit = (source[offset] >> (column % 8U)) & 1U;
            }
            target[(size_t)ty * drawable_width + tx] = bit ? (depth == 1U ? 1U : 0xffffffffU) : 0U;
        }
    }
    (void)depth;
}

void x_drawable_rectangle(struct x_server *server, xid_t drawable, int16_t x,
                          int16_t y, uint16_t width, uint16_t height,
                          uint32_t pixel) {
    if (width == 0U || height == 0U) return;
    x_drawable_fill(server, drawable, x, y, width, 1U, pixel);
    x_drawable_fill(server, drawable, x, (int16_t)(y + height - 1), width, 1U,
                    pixel);
    x_drawable_fill(server, drawable, x, y, 1U, height, pixel);
    x_drawable_fill(server, drawable, (int16_t)(x + width - 1), y, 1U, height,
                    pixel);
}

void x_drawable_line(struct x_server *server, xid_t drawable, int16_t x0,
                     int16_t y0, int16_t x1, int16_t y1, uint32_t pixel) {
    uint16_t width = 0U;
    uint16_t height = 0U;
    int is_window = 0;
    uint32_t *pixels = x_drawable_pixels(server, drawable, &width, &height,
                                         &is_window);
    if (!pixels) return;
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t steps = (dx < 0 ? -dx : dx);
    int32_t rise = (dy < 0 ? -dy : dy);
    if (rise > steps) steps = rise;
    if (steps <= 0) {
        if (x0 >= 0 && y0 >= 0 && x0 < (int32_t)width && y0 < (int32_t)height) {
            pixels[(size_t)y0 * width + (size_t)x0] = pixel;
        }
        return;
    }
    for (int32_t step = 0; step <= steps; ++step) {
        int32_t px = x0 + (dx * step) / steps;
        int32_t py = y0 + (dy * step) / steps;
        if (px < 0 || py < 0 || px >= (int32_t)width || py >= (int32_t)height) {
            continue;
        }
        pixels[(size_t)py * width + (size_t)px] = pixel;
    }
}

void x_rgb_to_pixel(uint16_t red, uint16_t green, uint16_t blue,
                    uint32_t *pixel) {
    if (!pixel) return;
    *pixel = ((uint32_t)(red >> 8) << 16) | ((uint32_t)(green >> 8) << 8) |
             (uint32_t)(blue >> 8);
}
