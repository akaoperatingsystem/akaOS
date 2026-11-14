#include <xscore/framebuffer.h>
#include <xscore/file.h>
#include <xscore/serial.h>
#include <xscore/vmm.h>
#include <akaos/boot_info.h>
#include <xscore/version.h>
#include <xscore/color.h>
#include <font_data.h>
#include <font8x16_data.h>
#include <stddef.h>
#include <stdint.h>

/* ================================================================
 * Framebuffer Canvas Operations
 * ================================================================ */

static struct canvas g_boot_canvas;
static int g_boot_canvas_valid = 0;
static uint32_t g_graphics_owners = 0;
static xscore_console_output_fn g_saved_console_sink = 0;

static uint32_t native_color(const struct canvas *canvas, uint32_t rgb) {
    if (!canvas || !canvas->requires_red_blue_swap) return rgb;
    return (rgb & 0x00ff00U) | ((rgb & 0xff0000U) >> 16) | ((rgb & 0x0000ffU) << 16);
}

static uint32_t rgb_color(const struct canvas *canvas, uint32_t native) {
    if (!canvas || !canvas->requires_red_blue_swap) return native;
    return (native & 0x00ff00U) | ((native & 0xff0000U) >> 16) | ((native & 0x0000ffU) << 16);
}

int xscore_canvas_init(struct canvas *canvas, void *pixels, uint32_t width,
                       uint32_t height, uint32_t stride, uint32_t pixel_format) {
    if (!canvas || !pixels || !width || !height) return 0;
    if (pixel_format > AKAOS_PIXEL_BGRX8888) return 0;
    canvas->pixels = (uint32_t *)pixels;
    canvas->width = width;
    canvas->height = height;
    canvas->stride = stride ? stride : width;
    canvas->requires_red_blue_swap = (pixel_format == AKAOS_PIXEL_RGBX8888);
    return 1;
}

void xscore_canvas_fill_rect(struct canvas *canvas, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height, uint32_t color) {
    if (!canvas || !canvas->pixels) return;
    if (x >= canvas->width || y >= canvas->height) return;
    if (x + width > canvas->width) width = canvas->width - x;
    if (y + height > canvas->height) height = canvas->height - y;
    uint32_t nc = native_color(canvas, color);
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t *pixel = canvas->pixels + ((y + row) * canvas->stride) + x;
        for (uint32_t column = 0; column < width; ++column) pixel[column] = nc;
    }
}

void xscore_canvas_blend_pixel(struct canvas *canvas, uint32_t x, uint32_t y,
                               uint32_t color, uint8_t alpha) {
    if (!canvas || !canvas->pixels || !alpha) return;
    if (x >= canvas->width || y >= canvas->height) return;
    uint32_t *pixel = canvas->pixels + y * canvas->stride + x;
    if (alpha == 255) {
        *pixel = native_color(canvas, color);
        return;
    }
    uint32_t background = rgb_color(canvas, *pixel);
    uint32_t red = ((((color >> 16) & 0xff) * alpha) + (((background >> 16) & 0xff) * (255 - alpha))) / 255;
    uint32_t green = ((((color >> 8) & 0xff) * alpha) + (((background >> 8) & 0xff) * (255 - alpha))) / 255;
    uint32_t blue = (((color & 0xff) * alpha) + ((background & 0xff) * (255 - alpha))) / 255;
    *pixel = native_color(canvas, (red << 16) | (green << 8) | blue);
}

void xscore_canvas_draw_wordmark(struct canvas *canvas, uint32_t x, uint32_t y,
                                 uint32_t color) {
    if (!canvas || !canvas->pixels) return;
    for (unsigned row = 0; row < AKAOS_LOGO_HEIGHT; ++row) {
        for (unsigned column = 0; column < AKAOS_LOGO_WIDTH; ++column) {
            uint8_t a = akaos_logo_alpha[row * AKAOS_LOGO_WIDTH + column];
            xscore_canvas_blend_pixel(canvas, x + column, y + row, color, a);
        }
    }
}

static uint32_t font_text_width(const char *text) {
    uint32_t width = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < AKAOS_FONT_FIRST || c > AKAOS_FONT_LAST) c = '?';
        unsigned idx = c - AKAOS_FONT_FIRST;
        width += (uint32_t)akaos_font_advances[idx] + 1;
    }
    return width;
}

static void font_draw_text(struct canvas *canvas, uint32_t x, uint32_t y,
                           uint32_t color, const char *text) {
    if (!canvas || !canvas->pixels) return;
    uint32_t cursor_x = x;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < AKAOS_FONT_FIRST || c > AKAOS_FONT_LAST) c = '?';
        unsigned idx = c - AKAOS_FONT_FIRST;
        const uint8_t *alpha = akaos_font_alpha + (size_t)idx * AKAOS_FONT_CELL_WIDTH * AKAOS_FONT_CELL_HEIGHT;
        for (unsigned r = 0; r < AKAOS_FONT_CELL_HEIGHT; ++r) {
            for (unsigned col = 0; col < AKAOS_FONT_CELL_WIDTH; ++col) {
                uint8_t a = alpha[r * AKAOS_FONT_CELL_WIDTH + col];
                if (a) {
                    xscore_canvas_blend_pixel(canvas, cursor_x + col, y + r, color, a);
                }
            }
        }
        cursor_x += (uint32_t)akaos_font_advances[idx] + 1;
    }
}

/* ================================================================
 * Bootsplash Spinner
 * ================================================================ */

#define SPINNER_RADIUS 16
#define SPINNER_DOT_SIZE 4

static const int8_t spinner_offsets[8][2] = {
    { 0, -16 }, { 11, -11 }, { 16, 0 }, { 11, 11 },
    { 0, 16 }, { -11, 11 }, { -16, 0 }, { -11, -11 }
};

static uint32_t spinner_center_x;
static uint32_t spinner_center_y;
static uint32_t spinner_phase;
static int splash_active = 0;

static void spinner_draw(void) {
    if (!g_boot_canvas_valid || !splash_active) return;
    uint32_t box = SPINNER_RADIUS + SPINNER_DOT_SIZE + 2;
    xscore_canvas_fill_rect(&g_boot_canvas, spinner_center_x - box, spinner_center_y - box,
                            box * 2 + 1, box * 2 + 1, 0x000000);
    for (unsigned dot = 0; dot < 8; ++dot) {
        unsigned lit = ((dot - spinner_phase + 8) % 8) < 4;
        uint32_t color = lit ? 0xe2e2e2 : 0x2e2e2e;
        xscore_canvas_fill_rect(&g_boot_canvas,
                                spinner_center_x + (uint32_t)spinner_offsets[dot][0] - SPINNER_DOT_SIZE / 2,
                                spinner_center_y + (uint32_t)spinner_offsets[dot][1] - SPINNER_DOT_SIZE / 2,
                                SPINNER_DOT_SIZE, SPINNER_DOT_SIZE, color);
    }
}

void xscore_splash_init(const struct akaos_boot_info *boot_info) {
    if (!boot_info || !boot_info->framebuffer.address) return;
    if (!xscore_canvas_init(&g_boot_canvas, (void *)(uintptr_t)boot_info->framebuffer.address,
                            boot_info->framebuffer.width, boot_info->framebuffer.height,
                            boot_info->framebuffer.pixels_per_scanline,
                            boot_info->framebuffer.pixel_format)) {
        return;
    }
    g_boot_canvas_valid = 1;

    xscore_canvas_fill_rect(&g_boot_canvas, 0, 0, g_boot_canvas.width, g_boot_canvas.height, 0x000000);

    uint32_t logo_x = g_boot_canvas.width > AKAOS_LOGO_WIDTH ? (g_boot_canvas.width - AKAOS_LOGO_WIDTH) / 2 : 0;
    uint32_t total_h = AKAOS_LOGO_HEIGHT + 16 + AKAOS_FONT_CELL_HEIGHT + 24 + SPINNER_RADIUS * 2;
    uint32_t logo_y = g_boot_canvas.height > total_h ? (g_boot_canvas.height - total_h) / 2 : 0;

    xscore_canvas_draw_wordmark(&g_boot_canvas, logo_x, logo_y, 0xf0f0f0);

    const char *subtitle = "Starting XSCore";
    uint32_t sub_y = logo_y + AKAOS_LOGO_HEIGHT + 16;
    uint32_t sub_w = font_text_width(subtitle);
    uint32_t sub_x = g_boot_canvas.width > sub_w ? (g_boot_canvas.width - sub_w) / 2 : 0;
    font_draw_text(&g_boot_canvas, sub_x, sub_y, 0x8b9bad, subtitle);

    spinner_center_x = g_boot_canvas.width / 2;
    spinner_center_y = sub_y + AKAOS_FONT_CELL_HEIGHT + 24 + SPINNER_RADIUS;
    spinner_phase = 0;
    splash_active = 1;
    spinner_draw();
}

void xscore_splash_step(void) {
    if (!splash_active) return;
    spinner_phase = (spinner_phase + 1) % 8;
    spinner_draw();
}

void xscore_splash_finish(void) {
    splash_active = 0;
}

/* ================================================================
 * Framebuffer Text Console (8x16 Monospace Grid)
 * ================================================================ */

#define SHELL_BACKGROUND 0x000000
#define SHELL_TEXT 0xe6edf3
#define SHELL_CELL_WIDTH AKAOS_VGA_FONT_CELL_WIDTH
#define SHELL_CELL_HEIGHT AKAOS_VGA_FONT_CELL_HEIGHT

static uint32_t shell_columns;
static uint32_t shell_rows;
static uint32_t shell_cursor_col;
static uint32_t shell_cursor_row;
static int shell_reverse_video = 0;
static uint32_t shell_fg_color = ANSI_DEFAULT_FG;
static uint32_t shell_bg_color = ANSI_DEFAULT_BG;
static uint32_t shell_fg_color = SHELL_TEXT;


static uint32_t get_ansi_color(int code) {
    switch (code) {
        case 30: return 0x000000; // Black
        case 31: return 0xff0000; // Red
        case 32: return 0x00ff00; // Green
        case 33: return 0xffff00; // Yellow
        case 34: return 0x0000ff; // Blue
        case 35: return 0xff00ff; // Magenta
        case 36: return 0x00ffff; // Cyan
        case 37: return 0xffffff; // White
        default: return SHELL_TEXT;
    }
}


static uint32_t get_ansi_color(int code) {
    switch (code) {
        case 30: case 40: return ANSI_COLOR_BLACK;
        case 31: case 41: return ANSI_COLOR_RED;
        case 32: case 42: return ANSI_COLOR_GREEN;
        case 33: case 43: return ANSI_COLOR_YELLOW;
        case 34: case 44: return ANSI_COLOR_BLUE;
        case 35: case 45: return ANSI_COLOR_MAGENTA;
        case 36: case 46: return ANSI_COLOR_CYAN;
        case 37: case 47: return ANSI_COLOR_WHITE;
        default: return ANSI_DEFAULT_FG;
    }
}

static void shell_grid_init(void) {
    if (!g_boot_canvas_valid) {
        shell_columns = 0;
        shell_rows = 0;
        return;
    }
    shell_columns = g_boot_canvas.width / SHELL_CELL_WIDTH;
    shell_rows = g_boot_canvas.height / SHELL_CELL_HEIGHT;
    if (shell_columns == 0) shell_columns = 1;
    if (shell_rows == 0) shell_rows = 1;
}

static void shell_scroll_one_row(void) {
    if (!g_boot_canvas_valid || shell_rows <= 1) return;
    uint32_t line_h = SHELL_CELL_HEIGHT;
    uint32_t copy_height = g_boot_canvas.height > line_h ? g_boot_canvas.height - line_h : 0;
    for (uint32_t y = 0; y < copy_height; ++y) {
        uint32_t *dst = g_boot_canvas.pixels + y * g_boot_canvas.stride;
        uint32_t *src = g_boot_canvas.pixels + (y + line_h) * g_boot_canvas.stride;
        for (uint32_t x = 0; x < g_boot_canvas.width; ++x) dst[x] = src[x];
    }
    xscore_canvas_fill_rect(&g_boot_canvas, 0, copy_height, g_boot_canvas.width, line_h, SHELL_BACKGROUND);
    if (shell_cursor_row != 0) --shell_cursor_row;
}

static void shell_scroll_if_needed(void) {
    if (!g_boot_canvas_valid || shell_rows == 0) return;
    while (shell_cursor_row >= shell_rows) {
        shell_scroll_one_row();
    }
}

static void shell_draw_glyph(uint32_t x, uint32_t y, uint32_t color, unsigned char ch) {
    if (!g_boot_canvas_valid) return;
    if (x + SHELL_CELL_WIDTH > g_boot_canvas.width ||
        y + SHELL_CELL_HEIGHT > g_boot_canvas.height) return;
    const uint8_t *glyph = akaos_vga_font + (size_t)ch * AKAOS_VGA_FONT_CELL_HEIGHT;
    uint32_t fg_val = shell_reverse_video ? SHELL_BACKGROUND : shell_fg_color;
    uint32_t bg_val = shell_reverse_video ? shell_fg_color : shell_bg_color;
    uint32_t fg = native_color(&g_boot_canvas, fg_val);
    uint32_t bg = native_color(&g_boot_canvas, bg_val);
    for (unsigned row = 0; row < SHELL_CELL_HEIGHT; ++row) {
        uint8_t bits = glyph[row];
        uint32_t *line = g_boot_canvas.pixels + (y + row) * g_boot_canvas.stride + x;
        for (unsigned col = 0; col < SHELL_CELL_WIDTH; ++col) {
            line[col] = (bits & (0x80U >> col)) ? fg : bg;
        }
    }
}

static void shell_put_char(unsigned char character, uint32_t color) {
    shell_scroll_if_needed();
    if (shell_cursor_col >= shell_columns) {
        shell_cursor_col = 0;
        ++shell_cursor_row;
        shell_scroll_if_needed();
    }
    if (shell_cursor_row >= shell_rows) return;
    shell_draw_glyph(shell_cursor_col * SHELL_CELL_WIDTH,
                     shell_cursor_row * SHELL_CELL_HEIGHT, color, character);
    ++shell_cursor_col;
    if (shell_cursor_col >= shell_columns) {
        shell_cursor_col = 0;
        ++shell_cursor_row;
    }
}

static int shell_escape_state = 0;
static int shell_csi_params[8];
static int shell_csi_param_count = 0;
static int shell_csi_has_param = 0;

static void handle_csi_command(char cmd) {
    if (cmd == 'J') {
        int param = (shell_csi_param_count > 0) ? shell_csi_params[0] : 0;
        if (param == 2 || param == 0) {
            if (g_boot_canvas_valid) {
                xscore_canvas_fill_rect(&g_boot_canvas, 0, 0, g_boot_canvas.width, g_boot_canvas.height, SHELL_BACKGROUND);
                shell_grid_init();
                shell_reverse_video = 0;
                shell_cursor_col = 0;
                shell_cursor_row = 0;
            }
        }
    } else if (cmd == 'H' || cmd == 'f') {
        int row = (shell_csi_param_count > 0 && shell_csi_params[0] > 0) ? shell_csi_params[0] - 1 : 0;
        int col = (shell_csi_param_count > 1 && shell_csi_params[1] > 0) ? shell_csi_params[1] - 1 : 0;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if ((uint32_t)row >= shell_rows) row = (int)shell_rows - 1;
        if ((uint32_t)col >= shell_columns) col = (int)shell_columns - 1;
        shell_cursor_row = (uint32_t)row;
        shell_cursor_col = (uint32_t)col;
    } else if (cmd == 'm') {
        if (shell_csi_param_count == 0) {
            shell_reverse_video = 0;
            shell_fg_color = SHELL_TEXT;
        } else {
            for (int i = 0; i < shell_csi_param_count; ++i) {
                int p = shell_csi_params[i];
                if (p == 0) { shell_reverse_video = 0; shell_fg_color = SHELL_TEXT; }
                else if (p == 7) shell_reverse_video = 1;
                else if (p == 27) shell_reverse_video = 0;
                else if (p >= 30 && p <= 37) shell_fg_color = get_ansi_color(p);
            }
        }
    } else if (cmd == 'K') {
        if (g_boot_canvas_valid && shell_cursor_row < shell_rows) {
            for (uint32_t col = shell_cursor_col; col < shell_columns; ++col) {
                shell_draw_glyph(col * SHELL_CELL_WIDTH, shell_cursor_row * SHELL_CELL_HEIGHT, SHELL_TEXT, ' ');
            }
        }
    }
}

void xscore_fb_console_sink(char c) {
    if (!g_boot_canvas_valid) return;

    if (shell_escape_state == 1) {
        if (c == '[') {
            shell_escape_state = 2;
            shell_csi_param_count = 0;
            shell_csi_has_param = 0;
            for (int i = 0; i < 8; ++i) shell_csi_params[i] = 0;
        } else {
            shell_escape_state = 0;
        }
        return;
    } else if (shell_escape_state == 2) {
        if (c >= '0' && c <= '9') {
            shell_csi_params[shell_csi_param_count] = shell_csi_params[shell_csi_param_count] * 10 + (c - '0');
            shell_csi_has_param = 1;
            return;
        } else if (c == ';') {
            if (shell_csi_has_param || shell_csi_param_count > 0) {
                if (shell_csi_param_count < 7) ++shell_csi_param_count;
            }
            shell_csi_has_param = 0;
            return;
        } else {
            if (shell_csi_has_param) ++shell_csi_param_count;
            handle_csi_command(c);
            shell_escape_state = 0;
            return;
        }
    }

    if (c == '\033') {
        shell_escape_state = 1;
        return;
    }

    if (c == '\r') {
        shell_cursor_col = 0;
        return;
    }
    if (c == '\n') {
        shell_cursor_col = 0;
        ++shell_cursor_row;
        shell_scroll_if_needed();
        return;
    }
    if (c == '\b') {
        if (shell_cursor_col > 0) {
            --shell_cursor_col;
            shell_draw_glyph(shell_cursor_col * SHELL_CELL_WIDTH,
                             shell_cursor_row * SHELL_CELL_HEIGHT, SHELL_TEXT, ' ');
        }
        return;
    }
    if (c == '\t') {
        uint32_t spaces = 4 - (shell_cursor_col % 4);
        for (uint32_t s = 0; s < spaces; ++s) shell_put_char(' ', SHELL_TEXT);
        return;
    }
    if ((unsigned char)c < 32 || (unsigned char)c == 127) return;

    shell_put_char((unsigned char)c, SHELL_TEXT);
}

void xscore_fb_console_init(const struct akaos_boot_info *boot_info) {
    if (!boot_info || !boot_info->framebuffer.address) return;
    if (!g_boot_canvas_valid) {
        if (!xscore_canvas_init(&g_boot_canvas, (void *)(uintptr_t)boot_info->framebuffer.address,
                                boot_info->framebuffer.width, boot_info->framebuffer.height,
                                boot_info->framebuffer.pixels_per_scanline,
                                boot_info->framebuffer.pixel_format)) {
            return;
        }
        g_boot_canvas_valid = 1;
    }
    xscore_splash_finish();
    xscore_canvas_fill_rect(&g_boot_canvas, 0, 0, g_boot_canvas.width, g_boot_canvas.height, SHELL_BACKGROUND);
    shell_grid_init();
    shell_cursor_col = 0;
    shell_cursor_row = 0;
    xscore_file_set_console_sink(xscore_fb_console_sink);
}

void xscore_framebuffer_register(const struct akaos_framebuffer *framebuffer) {
    if (!framebuffer || !framebuffer->address) return;
    if (xscore_canvas_init(&g_boot_canvas,
                           (void *)(uintptr_t)framebuffer->address,
                           framebuffer->width, framebuffer->height,
                           framebuffer->pixels_per_scanline,
                           framebuffer->pixel_format)) {
        g_boot_canvas_valid = 1;
    }
}

int xscore_fb_console_is_active(void) {
    return g_boot_canvas_valid;
}

int xscore_framebuffer_get_info(struct akaos_display_info *info) {
    if (!info || !g_boot_canvas_valid || !g_boot_canvas.pixels) return 0;
    info->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    info->width = g_boot_canvas.width;
    info->height = g_boot_canvas.height;
    info->stride_pixels = g_boot_canvas.stride;
    info->pixel_format = g_boot_canvas.requires_red_blue_swap
                             ? AKAOS_DISPLAY_PIXEL_RGBX8888
                             : AKAOS_DISPLAY_PIXEL_BGRX8888;
    info->reserved = 0;
    info->byte_size =
        (uint64_t)g_boot_canvas.stride * g_boot_canvas.height * 4;
    return 1;
}

int64_t xscore_framebuffer_write(uint64_t offset, const void *buffer,
                                 uint64_t length) {
    if (!g_boot_canvas_valid || !g_boot_canvas.pixels || !buffer) return -1;
    uint64_t size =
        (uint64_t)g_boot_canvas.stride * g_boot_canvas.height * 4;
    if (offset >= size) return 0;
    if (length > size - offset) length = size - offset;
    volatile uint8_t *destination =
        (volatile uint8_t *)(void *)g_boot_canvas.pixels + offset;
    const uint8_t *source = buffer;
    for (uint64_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
    return (int64_t)length;
}

void xscore_framebuffer_graphics_acquire(void) {
    if (!g_boot_canvas_valid) return;
    if (g_graphics_owners++ == 0) {
        g_saved_console_sink = xscore_file_get_console_sink();
        xscore_file_set_console_sink(0);
    }
}

void xscore_framebuffer_graphics_release(void) {
    if (!g_boot_canvas_valid || g_graphics_owners == 0) return;
    if (--g_graphics_owners == 0) {
        xscore_file_set_console_sink(g_saved_console_sink);
        g_saved_console_sink = 0;
    }
}

/* ================================================================
 * QEMU RAMFB Display Discovery (via fw_cfg DMA)
 * ================================================================ */

#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
static uint32_t g_ramfb_pixels[1024 * 768] __attribute__((aligned(4096)));

struct qemu_ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed));

struct qemu_fw_cfg_dma_access {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed, aligned(8)));

static inline uint32_t be32(uint32_t x) {
    return ((x & 0xffU) << 24) | ((x & 0xff00U) << 8) |
           ((x >> 8) & 0xff00U) | ((x >> 24) & 0xffU);
}

static inline uint64_t be64(uint64_t x) {
    return ((uint64_t)be32((uint32_t)x) << 32) | be32((uint32_t)(x >> 32));
}

#define FW_CFG_SIGNATURE       0x0000
#define FW_CFG_FILE_DIR        0x0019

#define FW_CFG_DMA_CTL_ERROR   0x01
#define FW_CFG_DMA_CTL_READ    0x02
#define FW_CFG_DMA_CTL_SKIP    0x04
#define FW_CFG_DMA_CTL_SELECT  0x08
#define FW_CFG_DMA_CTL_WRITE   0x10

static inline void fw_cfg_select(volatile uint8_t *base, uint16_t key) {
    volatile uint16_t *sel = (volatile uint16_t *)(base + 8);
    *sel = (uint16_t)((key >> 8) | (key << 8));
}

int xscore_ramfb_probe(uint64_t fw_cfg_base, struct akaos_framebuffer *fb_out) {
    if (!fw_cfg_base || !fb_out) return 0;

    /* Map fw_cfg MMIO space */
    for (uint64_t i = 0; i < 0x1000; i += 4096) {
        xscore_vmm_map_page(fw_cfg_base + i, fw_cfg_base + i, XSCORE_PAGE_WRITABLE);
    }

    volatile uint8_t *fw_cfg = (volatile uint8_t *)(uintptr_t)fw_cfg_base;

    /* Select FW_CFG_SIGNATURE (0x0000) */
    fw_cfg_select(fw_cfg, FW_CFG_SIGNATURE);

    char sig[4];
    for (int i = 0; i < 4; ++i) sig[i] = (char)fw_cfg[0];
    if (sig[0] != 'Q' || sig[1] != 'E' || sig[2] != 'M' || sig[3] != 'U') {
        return 0;
    }

    /* Select FW_CFG_FILE_DIR (0x0019) to find "etc/ramfb" */
    fw_cfg_select(fw_cfg, FW_CFG_FILE_DIR);

    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) {
        count = (count << 8) | (uint8_t)fw_cfg[0];
    }
    if (count > 256) count = 256;

    uint16_t ramfb_select = 0;
    for (uint32_t f = 0; f < count; ++f) {
        uint8_t file_hdr[64];
        for (int b = 0; b < 64; ++b) file_hdr[b] = fw_cfg[0];
        const char *name = (const char *)(file_hdr + 8);
        if (name[0] == 'e' && name[1] == 't' && name[2] == 'c' && name[3] == '/' &&
            name[4] == 'r' && name[5] == 'a' &&
            name[6] == 'm' && name[7] == 'f' && name[8] == 'b' && name[9] == 0) {
            ramfb_select = (uint16_t)((file_hdr[4] << 8) | file_hdr[5]);
            break;
        }
    }

    if (!ramfb_select) return 0;

    /* Prepare ramfb config */
    static struct qemu_ramfb_cfg ramfb_cfg;
    ramfb_cfg.addr = be64((uint64_t)(uintptr_t)g_ramfb_pixels);
    ramfb_cfg.fourcc = be32(0x34325258); /* "XR24" = DRM_FORMAT_XRGB8888 (0x58523234) */
    ramfb_cfg.flags = 0;
    ramfb_cfg.width = be32(1024);
    ramfb_cfg.height = be32(768);
    ramfb_cfg.stride = be32(1024 * 4);

    /* Send via fw_cfg DMA */
    static struct qemu_fw_cfg_dma_access dma;
    dma.control = be32(((uint32_t)ramfb_select << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE);
    dma.length = be32(sizeof(ramfb_cfg));
    dma.address = be64((uint64_t)(uintptr_t)&ramfb_cfg);

    /* Map DMA control structures */
    xscore_vmm_map_page((uint64_t)(uintptr_t)&dma, (uint64_t)(uintptr_t)&dma, XSCORE_PAGE_WRITABLE);
    xscore_vmm_map_page((uint64_t)(uintptr_t)&ramfb_cfg, (uint64_t)(uintptr_t)&ramfb_cfg, XSCORE_PAGE_WRITABLE);

    /* Trigger DMA write */
#if UINTPTR_MAX > 0xFFFFFFFFULL
    volatile uint64_t *dma_reg = (volatile uint64_t *)(fw_cfg + 16);
    *dma_reg = be64((uint64_t)(uintptr_t)&dma);
#else
    volatile uint32_t *dma_reg_hi = (volatile uint32_t *)(fw_cfg + 16);
    volatile uint32_t *dma_reg_lo = (volatile uint32_t *)(fw_cfg + 20);
    *dma_reg_hi = be32((uint32_t)((uint64_t)(uintptr_t)&dma >> 32));
    *dma_reg_lo = be32((uint32_t)(uintptr_t)&dma);
#endif

    /* Wait for DMA completion */
    while (be32(dma.control) & ~FW_CFG_DMA_CTL_ERROR) {}
    if (be32(dma.control) & FW_CFG_DMA_CTL_ERROR) {
        return 0;
    }

    fb_out->address = (uint64_t)(uintptr_t)g_ramfb_pixels;
    fb_out->width = 1024;
    fb_out->height = 768;
    fb_out->pixels_per_scanline = 1024;
    fb_out->pixel_format = AKAOS_PIXEL_BGRX8888;
    return 1;
}
#else
int xscore_ramfb_probe(uint64_t fw_cfg_base, struct akaos_framebuffer *fb_out) {
    (void)fw_cfg_base;
    (void)fb_out;
    return 0;
}
#endif
