/* ============================================================
 * akaOS — VGA Text Mode Driver Header
 * ============================================================ */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/* VGA text mode dimensions */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA color constants */
enum vga_color {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
};

/* Initialize VGA driver, clear screen */
void vga_init(void);

/* Clear the entire screen */
void vga_clear(void);

/* Print a single character at current cursor position */
void vga_putchar(char c);

/* Print a null-terminated string */
void vga_print(const char *str);

/* Print a string with specific foreground and background colors */
void vga_print_color(const char *str, enum vga_color fg, enum vga_color bg);

/* Set the default foreground and background colors */
void vga_set_color(enum vga_color fg, enum vga_color bg);

/* Move to a new line */
void vga_newline(void);

/* Set cursor position */
void vga_set_cursor(int row, int col);

/* Handle backspace */
void vga_backspace(void);

#endif /* VGA_H */
