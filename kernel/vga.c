/* ============================================================
 * akaOS — VGA Driver (Framebuffer + Text Mode Fallback)
 * ============================================================ */
#include "vga.h"
#include "io.h"
#include "string.h"
#include "fb.h"

extern volatile int gui_mode_active;
extern void gui_term_putchar(char c);
extern void gui_term_set_color(uint32_t fg);
extern void gui_term_clear(void);

/* Text mode VGA memory (fallback when no framebuffer) */
extern uint64_t hhdm_offset;
static uint16_t *TEXT_VGA = 0;
static int use_framebuffer = 0;

/* VGA color → RGB */
static const uint32_t vga_rgb[16] = {
    0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
    0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF,
};
static const uint32_t gui_rgb[16] = {
    0x1a1b26,0x7aa2f7,0x9ece6a,0x7dcfff,0xf7768e,0xbb9af7,0xe0af68,0xa9b1d6,
    0x565f89,0x7aa2f7,0x9ece6a,0x7dcfff,0xf7768e,0xbb9af7,0xe0af68,0xc0caf5,
};

static int cur_row = 0, cur_col = 0;
static int max_cols = 80, max_rows = 25;
static uint8_t text_color = 0x07; /* light grey on black */
static uint32_t fb_fg = 0xAAAAAA, fb_bg = 0x000000;
static enum vga_color cur_fg_idx = VGA_LIGHT_GREY;

#define FB_SCALE 2

/* --- Text mode helpers (0xB8000) --- */
static inline uint16_t text_entry(char c, uint8_t clr) {
    return (uint16_t)c | ((uint16_t)clr << 8);
}
static inline uint8_t text_clr(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}
static void text_update_cursor(void) {
    uint16_t pos = cur_row * 80 + cur_col;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
static void text_scroll(void) {
    for (int i = 0; i < 80 * 24; i++) TEXT_VGA[i] = TEXT_VGA[i + 80];
    for (int i = 80 * 24; i < 80 * 25; i++) TEXT_VGA[i] = text_entry(' ', text_color);
}

/* --- Framebuffer text helpers --- */
static void fb_scroll_text(void) {
    fb_scroll_region(0, 0, (int)fb_width(), max_rows * 8 * FB_SCALE, 1, 8 * FB_SCALE, fb_bg);
}

void vga_init(void) {
    /* Initialize VGA text buffer with HHDM offset for higher-half kernel */
    TEXT_VGA = (uint16_t *)(hhdm_offset + 0xB8000);

    use_framebuffer = (fb_width() > 0 && fb_height() > 0);
    if (use_framebuffer) {
        max_cols = (int)fb_width() / (8 * FB_SCALE);
        max_rows = (int)fb_height() / (8 * FB_SCALE);
        fb_fg = vga_rgb[VGA_LIGHT_GREY];
        fb_bg = vga_rgb[VGA_BLACK];
        fb_clear(fb_bg);
    } else {
        max_cols = 80;
        max_rows = 25;
        text_color = text_clr(VGA_LIGHT_GREY, VGA_BLACK);
        for (int i = 0; i < 80 * 25; i++) TEXT_VGA[i] = text_entry(' ', text_color);
    }
    cur_row = 0; cur_col = 0;
    cur_fg_idx = VGA_LIGHT_GREY;
}

void vga_clear(void) {
    if (gui_mode_active) { gui_term_clear(); return; }
    if (use_framebuffer) fb_clear(fb_bg);
    else for (int i = 0; i < 80*25; i++) TEXT_VGA[i] = text_entry(' ', text_color);
    cur_row = 0; cur_col = 0;
    if (!use_framebuffer) text_update_cursor();
}

void vga_putchar(char c) {
    if (gui_mode_active) { gui_term_putchar(c); return; }

    if (c == '\n') { vga_newline(); return; }
    if (c == '\t') { int s=4-(cur_col%4); for(int i=0;i<s;i++) vga_putchar(' '); return; }
    if (c == '\r') { cur_col = 0; return; }

    if (use_framebuffer) {
        fb_draw_char(cur_col*8*FB_SCALE, cur_row*8*FB_SCALE, c, fb_fg, fb_bg, FB_SCALE);
    } else {
        TEXT_VGA[cur_row * 80 + cur_col] = text_entry(c, text_color);
    }

    cur_col++;
    if (cur_col >= max_cols) { cur_col = 0; cur_row++; }
    if (cur_row >= max_rows) {
        if (use_framebuffer) fb_scroll_text(); else text_scroll();
        cur_row = max_rows - 1;
    }
    if (!use_framebuffer) text_update_cursor();
}

void vga_print(const char *str) { while (*str) vga_putchar(*str++); }

void vga_print_color(const char *str, enum vga_color fg, enum vga_color bg) {
    if (gui_mode_active) {
        uint32_t sfg = fb_fg;
        fb_fg = gui_rgb[fg]; gui_term_set_color(gui_rgb[fg]);
        vga_print(str);
        fb_fg = sfg; gui_term_set_color(gui_rgb[cur_fg_idx]);
        return;
    }
    if (use_framebuffer) {
        uint32_t sf=fb_fg, sb=fb_bg;
        fb_fg=vga_rgb[fg]; fb_bg=vga_rgb[bg];
        vga_print(str);
        fb_fg=sf; fb_bg=sb;
    } else {
        uint8_t sc = text_color;
        text_color = text_clr(fg, bg);
        vga_print(str);
        text_color = sc;
    }
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    cur_fg_idx = fg;
    if (gui_mode_active) { fb_fg=gui_rgb[fg]; gui_term_set_color(gui_rgb[fg]); }
    else if (use_framebuffer) { fb_fg=vga_rgb[fg]; fb_bg=vga_rgb[bg]; }
    else text_color = text_clr(fg, bg);
}

void vga_newline(void) {
    if (gui_mode_active) { gui_term_putchar('\n'); return; }
    cur_col = 0; cur_row++;
    if (cur_row >= max_rows) {
        if (use_framebuffer) fb_scroll_text(); else text_scroll();
        cur_row = max_rows - 1;
    }
    if (!use_framebuffer) text_update_cursor();
}

void vga_set_cursor(int row, int col) { cur_row=row; cur_col=col; }

void vga_backspace(void) {
    if (gui_mode_active) { gui_term_putchar('\b'); return; }
    if (cur_col > 0) cur_col--;
    else if (cur_row > 0) { cur_row--; cur_col = max_cols - 1; }
    if (use_framebuffer)
        fb_draw_char(cur_col*8*FB_SCALE, cur_row*8*FB_SCALE, ' ', fb_fg, fb_bg, FB_SCALE);
    else {
        TEXT_VGA[cur_row*80+cur_col] = text_entry(' ', text_color);
        text_update_cursor();
    }
}
