/* ============================================================
 * akaOS — Framebuffer Driver
 * ============================================================ */
#include "fb.h"
#include "font.h"
#include "multiboot2.h"
#include "string.h"

static uint32_t *fb_addr = 0;
static uint32_t fb_w = 0, fb_h = 0, fb_pitch = 0;
static uint32_t max_w = 0, max_h = 0;
static uint8_t  fb_bpp = 0;

static int fb_scale = 1;
static int fb_color_depth = 32;
static uint32_t logical_w = 0, logical_h = 0;

/* Double buffer */
#define FB_MAX_W 1920
#define FB_MAX_H 1080
static uint32_t backbuf[FB_MAX_W * FB_MAX_H];

#include "limine.h"

int fb_init_limine(struct limine_framebuffer *fb) {
    if (!fb) return -1;
    fb_addr  = (uint32_t *)fb->address;
    fb_w     = fb->width;
    fb_h     = fb->height;
    fb_pitch = fb->pitch;
    fb_bpp   = fb->bpp;
    max_w    = fb_w;
    max_h    = fb_h;
    if (fb_w > FB_MAX_W) fb_w = FB_MAX_W;
    if (fb_h > FB_MAX_H) fb_h = FB_MAX_H;
    logical_w = fb_w;
    logical_h = fb_h;
    memset(backbuf, 0, fb_w * fb_h * 4);
    return 0;
}

int fb_init(uint64_t mb2_addr) {
    struct mb2_tag *tag = mb2_find_tag(mb2_addr, MB2_TAG_FRAMEBUFFER);
    if (!tag) return -1;
    struct mb2_tag_framebuffer *fb = (struct mb2_tag_framebuffer *)tag;
    fb_addr  = (uint32_t *)(uintptr_t)fb->framebuffer_addr;
    fb_w     = fb->framebuffer_width;
    fb_h     = fb->framebuffer_height;
    fb_pitch = fb->framebuffer_pitch;
    fb_bpp   = fb->framebuffer_bpp;
    max_w    = fb_w;
    max_h    = fb_h;
    if (fb_w > FB_MAX_W) fb_w = FB_MAX_W;
    if (fb_h > FB_MAX_H) fb_h = FB_MAX_H;
    logical_w = fb_w;
    logical_h = fb_h;
    memset(backbuf, 0, fb_w * fb_h * 4);
    return 0;
}

uint32_t fb_width(void)  { return logical_w; }
uint32_t fb_height(void) { return logical_h; }
uint32_t *fb_get_backbuffer(void) { return backbuf; }

int fb_get_scale(void) { return fb_scale; }

void fb_set_render_params(int scale, int depth) {
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    fb_scale = scale;
    fb_color_depth = depth;
    logical_w = fb_w / fb_scale;
    logical_h = fb_h / fb_scale;
}

void fb_set_resolution(uint32_t w, uint32_t h) {
    if (w < 320) w = 320;
    if (h < 240) h = 240;
    if (w > FB_MAX_W) w = FB_MAX_W;
    if (h > FB_MAX_H) h = FB_MAX_H;
    if (w > max_w) w = max_w; /* Never exceed physical screen bounds */
    if (h > max_h) h = max_h;
    fb_w = w;
    fb_h = h;
    logical_w = fb_w / fb_scale;
    logical_h = fb_h / fb_scale;
    memset(backbuf, 0, logical_w * logical_h * 4);
}

static inline void bb_pixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < (int)logical_w && y >= 0 && y < (int)logical_h)
        backbuf[y * logical_w + x] = c;
}

void fb_put_pixel(int x, int y, uint32_t color) { bb_pixel(x, y, color); }

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            bb_pixel(i, j, color);
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    fb_draw_hline(x, y, w, color);
    fb_draw_hline(x, y + h - 1, w, color);
    for (int j = y; j < y + h; j++) {
        bb_pixel(x, j, color);
        bb_pixel(x + w - 1, j, color);
    }
}

void fb_draw_hline(int x, int y, int w, uint32_t color) {
    for (int i = x; i < x + w; i++) bb_pixel(i, y, color);
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale) {
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *glyph = font8x8[c - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            uint32_t clr = (bits & (0x80 >> col)) ? fg : bg;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++)
                    bb_pixel(x + col*scale + dx, y + row*scale + dy, clr);
        }
    }
}

void fb_draw_char_nobg(int x, int y, char c, uint32_t fg, int scale) {
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *glyph = font8x8[c - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col))
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        bb_pixel(x + col*scale + dx, y + row*scale + dy, fg);
        }
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg, int scale) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { y += FONT_H * scale; cx = x; s++; continue; }
        fb_draw_char(cx, y, *s, fg, bg, scale);
        cx += FONT_W * scale;
        s++;
    }
}

void fb_draw_string_nobg(int x, int y, const char *s, uint32_t fg, int scale) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { y += FONT_H * scale; cx = x; s++; continue; }
        fb_draw_char_nobg(cx, y, *s, fg, scale);
        cx += FONT_W * scale;
        s++;
    }
}

void fb_clear(uint32_t color) {
    for (uint32_t i = 0; i < logical_w * logical_h; i++) backbuf[i] = color;
}

void fb_flip(void) {
    if (!fb_addr) return;
    uint32_t pitch_px = fb_pitch / 4;
    
    for (uint32_t ly = 0; ly < logical_h; ly++) {
        uint32_t py_base = ly * fb_scale;
        if (py_base >= fb_h) break;
        
        uint32_t *dst_line = &fb_addr[py_base * pitch_px];
        for (uint32_t lx = 0; lx < logical_w; lx++) {
            uint32_t c = backbuf[ly * logical_w + lx];
            
            /* Apply color depth reduction */
            if (fb_color_depth == 16) {
                uint8_t r = (c >> 16) & 0xF8;
                uint8_t g = (c >> 8) & 0xFC;
                uint8_t b = c & 0xF8;
                c = (r << 16) | (g << 8) | b;
            } else if (fb_color_depth == 24) {
                c &= 0xFFFFFF; /* Ignore alpha, full 24-bit */
            }
            
            uint32_t px_base = lx * fb_scale;
            for (int dx = 0; dx < fb_scale && (px_base + dx) < fb_w; dx++) {
                dst_line[px_base + dx] = c;
            }
        }
        
        /* Copy the fully rendered physical line to the other scaled lines below it */
        for (int dy = 1; dy < fb_scale && (py_base + dy) < fb_h; dy++) {
            memcpy(&fb_addr[(py_base + dy) * pitch_px], dst_line, fb_w * 4);
        }
    }
}

void fb_scroll_region(int x, int y, int w, int h, int lines, int char_h, uint32_t bg) {
    int scroll_px = lines * char_h;
    for (int j = y; j < y + h - scroll_px; j++)
        for (int i = x; i < x + w; i++)
            backbuf[j * logical_w + i] = backbuf[(j + scroll_px) * logical_w + i];
    fb_fill_rect(x, y + h - scroll_px, w, scroll_px, bg);
}
