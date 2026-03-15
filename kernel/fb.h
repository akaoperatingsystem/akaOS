/* ============================================================
 * akaOS — Framebuffer Driver Header
 * ============================================================ */

#ifndef FB_H
#define FB_H

#include <stdint.h>

/* Common colors (RGB) */
#define COLOR_BLACK       0x000000
#define COLOR_WHITE       0xFFFFFF
#define COLOR_RED         0xFF0000
#define COLOR_GREEN       0x00FF00
#define COLOR_BLUE        0x0000FF
#define COLOR_DARK_BLUE   0x1a1a2e
#define COLOR_MID_BLUE    0x16213e
#define COLOR_ACCENT      0x0f3460
#define COLOR_CYAN        0x00AAAA
#define COLOR_YELLOW      0xFFFF55
#define COLOR_GREY        0xAAAAAA
#define COLOR_DARK_GREY   0x333333
#define COLOR_LIGHT_GREY  0xCCCCCC
#define COLOR_TASKBAR     0x1e1e2e
#define COLOR_TITLE_BAR   0x2d2d44
#define COLOR_TITLE_ACTIVE 0x4a4a6a
#define COLOR_TERM_BG     0x1a1b26
#define COLOR_TERM_FG     0xa9b1d6
#define COLOR_TERM_GREEN  0x9ece6a
#define COLOR_TERM_CYAN   0x7dcfff
#define COLOR_TERM_RED    0xf7768e
#define COLOR_TERM_YELLOW 0xe0af68
#define COLOR_TERM_BLUE   0x7aa2f7
#define COLOR_TERM_PURPLE 0xbb9af7

/* Initialize framebuffer */
int fb_init(uint64_t mb2_addr); // Legacy
struct limine_framebuffer;
int fb_init_limine(struct limine_framebuffer *fb);

/* Basic drawing */
void fb_put_pixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_hline(int x, int y, int w, uint32_t color);

/* Text drawing (8x8 font with scaling) */
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale);
void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg, int scale);
void fb_draw_char_nobg(int x, int y, char c, uint32_t fg, int scale);
void fb_draw_string_nobg(int x, int y, const char *s, uint32_t fg, int scale);

/* Buffer operations */
void fb_clear(uint32_t color);
void fb_flip(void);         /* Copy backbuffer to screen */
void fb_scroll_region(int x, int y, int w, int h, int lines, int char_h, uint32_t bg);

/* Getters */
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t *fb_get_backbuffer(void);
int fb_get_scale(void);
void fb_set_render_params(int scale, int depth);
void fb_set_resolution(uint32_t w, uint32_t h);

#endif
