#ifndef XSCORE_FRAMEBUFFER_H
#define XSCORE_FRAMEBUFFER_H

#include <akaos/boot_info.h>
#include <akaos/display.h>
#include <stdint.h>
#include <stddef.h>

struct canvas {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t requires_red_blue_swap;
};

/* Core canvas rendering */
int xscore_canvas_init(struct canvas *canvas, void *pixels, uint32_t width,
                       uint32_t height, uint32_t stride, uint32_t pixel_format);
void xscore_canvas_fill_rect(struct canvas *canvas, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height, uint32_t color);
void xscore_canvas_blend_pixel(struct canvas *canvas, uint32_t x, uint32_t y,
                               uint32_t color, uint8_t alpha);
void xscore_canvas_draw_wordmark(struct canvas *canvas, uint32_t x, uint32_t y,
                                 uint32_t color);

/* Graphical Bootsplash & Console */
void xscore_splash_init(const struct akaos_boot_info *boot_info);
void xscore_splash_step(void);
void xscore_splash_finish(void);

void xscore_fb_console_init(const struct akaos_boot_info *boot_info);
void xscore_fb_console_sink(char c);
int xscore_fb_console_is_active(void);
void xscore_framebuffer_register(const struct akaos_framebuffer *framebuffer);
int xscore_framebuffer_get_info(struct akaos_display_info *info);
int64_t xscore_framebuffer_write(uint64_t offset, const void *buffer,
                                 uint64_t length);
void xscore_framebuffer_graphics_acquire(void);
void xscore_framebuffer_graphics_release(void);

/* PCI VGA / Bochs Display probe helper for ARM & RISC-V */
int xscore_pci_vga_probe(uint64_t ecam_base, struct akaos_framebuffer *fb_out);
int xscore_ramfb_probe(uint64_t fw_cfg_base, struct akaos_framebuffer *fb_out);

#endif
