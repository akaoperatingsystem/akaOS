/* Authentic Desktop Environment Renderer for akaOS X11 Display Server.
 *
 * Renders the authentic desktop wallpaper, desktop icons, taskbar panel,
 * application menu launcher, and open application window directly onto the
 * X11 root window backing store.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <font8x16_data.h>
#include "server.h"

#define X_FONT_CELL_WIDTH 8
#define X_FONT_CELL_HEIGHT 16
#define X_FONT_FIRST_CHAR 32
#define X_FONT_LAST_CHAR 126

static void desktop_put_pixel(struct x_server *server, int16_t x, int16_t y, uint32_t color) {
    if (x < 0 || y < 0 || x >= server->width || y >= server->height) return;
    int root = x_window_index(server, X_ROOT_WINDOW_ID);
    if (root < 0 || !server->windows[root].backing) return;
    server->windows[root].backing[(size_t)y * server->width + x] = color;
}

static void desktop_fill_rect(struct x_server *server, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, uint32_t color) {
    int root = x_window_index(server, X_ROOT_WINDOW_ID);
    if (root < 0 || !server->windows[root].backing) return;
    uint32_t *pixels = server->windows[root].backing;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > server->width ? server->width : x + w;
    int32_t y1 = y + h > server->height ? server->height : y + h;
    for (int32_t row = y0; row < y1; ++row) {
        uint32_t *line = pixels + (size_t)row * server->width;
        for (int32_t col = x0; col < x1; ++col) {
            line[col] = color;
        }
    }
}

static void desktop_draw_rect(struct x_server *server, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, uint32_t color) {
    desktop_fill_rect(server, x, y, w, 1, color);
    desktop_fill_rect(server, x, y + h - 1, w, 1, color);
    desktop_fill_rect(server, x, y, 1, h, color);
    desktop_fill_rect(server, x + w - 1, y, 1, h, color);
}

static void desktop_draw_glyph(struct x_server *server, int16_t x, int16_t y,
                              char c, uint32_t color) {
    uint8_t ch = (uint8_t)c;
    if (ch < X_FONT_FIRST_CHAR || ch > X_FONT_LAST_CHAR) ch = '?';
    const uint8_t *glyph = akaos_vga_font + (size_t)ch * X_FONT_CELL_HEIGHT;
    for (int row = 0; row < X_FONT_CELL_HEIGHT; ++row) {
        uint8_t bits = glyph[row];
        if (!bits) continue;
        int16_t py = y + row;
        for (int col = 0; col < X_FONT_CELL_WIDTH; ++col) {
            if (bits & (0x80U >> col)) {
                desktop_put_pixel(server, x + col, py, color);
            }
        }
    }
}

static void desktop_draw_text(struct x_server *server, int16_t x, int16_t y,
                             const char *text, uint32_t color) {
    if (!text) return;
    int16_t cur_x = x;
    while (*text) {
        desktop_draw_glyph(server, cur_x, y, *text, color);
        cur_x += X_FONT_CELL_WIDTH;
        ++text;
    }
}

static void desktop_draw_shadowed_text(struct x_server *server, int16_t x, int16_t y,
                                      const char *text, uint32_t fg, uint32_t shadow) {
    desktop_draw_text(server, x + 1, y + 1, text, shadow);
    desktop_draw_text(server, x, y, text, fg);
}

/* Icon type definitions */
enum {
    ICON_COMPUTER,
    ICON_FOLDER,
    ICON_TERMINAL,
    ICON_SETTINGS,
    ICON_APP_MENU,
    ICON_FILE_TEXT
};

static void desktop_draw_icon_32(struct x_server *server, int16_t x, int16_t y, int type) {
    switch (type) {
    case ICON_COMPUTER:
        /* Monitor frame */
        desktop_fill_rect(server, x + 2, y + 2, 28, 20, 0x0094a3b8);
        desktop_draw_rect(server, x + 1, y + 1, 30, 22, 0x00475569);
        /* Screen */
        desktop_fill_rect(server, x + 4, y + 4, 24, 16, 0x000284c7);
        /* Stand */
        desktop_fill_rect(server, x + 13, y + 23, 6, 5, 0x0064748b);
        desktop_fill_rect(server, x + 8, y + 28, 16, 2, 0x00475569);
        break;
    case ICON_FOLDER:
        /* Folder tab */
        desktop_fill_rect(server, x + 2, y + 4, 12, 4, 0x00f59e0b);
        /* Folder body */
        desktop_fill_rect(server, x + 2, y + 8, 28, 20, 0x00d97706);
        desktop_draw_rect(server, x + 1, y + 7, 30, 22, 0x00b45309);
        /* Inside document hint */
        desktop_fill_rect(server, x + 6, y + 12, 20, 12, 0x00fef3c7);
        break;
    case ICON_TERMINAL:
        /* Console box */
        desktop_fill_rect(server, x + 2, y + 2, 28, 26, 0x000f172a);
        desktop_draw_rect(server, x + 1, y + 1, 30, 28, 0x00334155);
        /* Prompt '>_' */
        desktop_draw_text(server, x + 5, y + 6, ">_", 0x0022c55e);
        break;
    case ICON_SETTINGS:
        /* Gear outer ring */
        desktop_fill_rect(server, x + 4, y + 4, 24, 24, 0x000ea5e9);
        desktop_draw_rect(server, x + 3, y + 3, 26, 26, 0x000284c7);
        /* Gear teeth */
        desktop_fill_rect(server, x + 12, y + 1, 8, 3, 0x000284c7);
        desktop_fill_rect(server, x + 12, y + 28, 8, 3, 0x000284c7);
        desktop_fill_rect(server, x + 1, y + 12, 3, 8, 0x000284c7);
        desktop_fill_rect(server, x + 28, y + 12, 3, 8, 0x000284c7);
        /* Center hole */
        desktop_fill_rect(server, x + 11, y + 11, 10, 10, 0x001e293b);
        break;
    default:
        desktop_fill_rect(server, x + 4, y + 4, 24, 24, 0x00cbd5e1);
        break;
    }
}

static void desktop_draw_icon_16(struct x_server *server, int16_t x, int16_t y, int type) {
    switch (type) {
    case ICON_APP_MENU:
        desktop_fill_rect(server, x + 1, y + 1, 6, 6, 0x003b82f6); /* Blue */
        desktop_fill_rect(server, x + 9, y + 1, 6, 6, 0x00ef4444); /* Red */
        desktop_fill_rect(server, x + 1, y + 9, 6, 6, 0x0022c55e); /* Green */
        desktop_fill_rect(server, x + 9, y + 9, 6, 6, 0x00eab308); /* Yellow */
        break;
    case ICON_FOLDER:
        desktop_fill_rect(server, x + 1, y + 2, 6, 2, 0x00f59e0b);
        desktop_fill_rect(server, x + 1, y + 4, 14, 10, 0x00d97706);
        break;
    case ICON_TERMINAL:
        desktop_fill_rect(server, x + 1, y + 1, 14, 14, 0x000f172a);
        desktop_draw_text(server, x + 2, y + 2, ">", 0x0022c55e);
        break;
    case ICON_FILE_TEXT:
        desktop_fill_rect(server, x + 2, y + 1, 12, 14, 0x00f8fafc);
        desktop_draw_rect(server, x + 1, y, 14, 16, 0x0094a3b8);
        desktop_fill_rect(server, x + 4, y + 4, 8, 1, 0x0064748b);
        desktop_fill_rect(server, x + 4, y + 7, 8, 1, 0x0064748b);
        desktop_fill_rect(server, x + 4, y + 10, 8, 1, 0x0064748b);
        break;
    default:
        desktop_fill_rect(server, x + 2, y + 2, 12, 12, 0x0060a5fa);
        break;
    }
}

void x_render_authentic_desktop(struct x_server *server, const char *desktop_env) {
    if (!server || !server->screen || server->width <= 0 || server->height <= 0) return;
    int root = x_window_index(server, X_ROOT_WINDOW_ID);
    if (root < 0 || !server->windows[root].backing) return;

    int is_lxde = (!desktop_env || strcmp(desktop_env, "lxde") == 0);
    int is_xfce = (desktop_env && strcmp(desktop_env, "xfce") == 0);
    int is_lxqt = (desktop_env && strcmp(desktop_env, "lxqt") == 0);

    /* 1. Render Wallpaper */
    for (int32_t y = 0; y < server->height; ++y) {
        uint32_t *line = server->windows[root].backing + (size_t)y * server->width;
        for (int32_t x = 0; x < server->width; ++x) {
            uint32_t r, g, b;
            if (is_lxde) {
                /* Classic LXDE Royal Blue Gradient */
                r = 20 + y * 25 / server->height;
                g = 45 + y * 65 / server->height;
                b = 90 + y * 140 / server->height;
            } else if (is_xfce) {
                /* XFCE Metallic Slate/Navy Gradient */
                r = 28 + y * 20 / server->height;
                g = 36 + y * 32 / server->height;
                b = 55 + y * 55 / server->height;
            } else if (is_lxqt) {
                /* LXQt Cyan/Dark Blue Tech Gradient */
                r = 14 + y * 18 / server->height;
                g = 38 + y * 55 / server->height;
                b = 72 + y * 95 / server->height;
            } else {
                /* Openbox Dark Textured Slate */
                r = 30 + ((x ^ y) & 2);
                g = 32 + ((x ^ y) & 2);
                b = 38 + ((x ^ y) & 2);
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            line[x] = (r << 16) | (g << 8) | b;
        }
    }

    /* 2. Render Desktop Icons */
    struct {
        int x, y, icon;
        const char *label;
    } desktop_icons[] = {
        { 24, 24,  ICON_COMPUTER, "Computer" },
        { 24, 114, ICON_FOLDER,   "Home" },
        { 24, 204, ICON_TERMINAL, "Terminal" },
        { 24, 294, ICON_SETTINGS, "Settings" },
    };
    for (size_t i = 0; i < sizeof(desktop_icons)/sizeof(desktop_icons[0]); ++i) {
        desktop_draw_icon_32(server, desktop_icons[i].x + 12, desktop_icons[i].y, desktop_icons[i].icon);
        desktop_draw_shadowed_text(server, desktop_icons[i].x, desktop_icons[i].y + 36,
                                  desktop_icons[i].label, 0x00f8fafc, 0x000f172a);
    }

    /* 3. Render Desktop Panel Bar */
    int16_t panel_h = 36;
    int16_t panel_y = server->height - panel_h;
    desktop_fill_rect(server, 0, panel_y, server->width, panel_h, 0x001e2430);
    desktop_fill_rect(server, 0, panel_y, server->width, 1, 0x003b82f6); /* Top blue highlight */

    /* Panel Start Menu Button */
    desktop_fill_rect(server, 4, panel_y + 3, 116, 30, 0x002d3748);
    desktop_draw_rect(server, 4, panel_y + 3, 116, 30, 0x00475569);
    desktop_draw_icon_16(server, 10, panel_y + 10, ICON_APP_MENU);
    desktop_draw_text(server, 30, panel_y + 10, "Applications", 0x00f8fafc);

    /* Quick Launch Icons */
    desktop_draw_icon_16(server, 130, panel_y + 10, ICON_FOLDER);
    desktop_draw_icon_16(server, 156, panel_y + 10, ICON_TERMINAL);
    desktop_fill_rect(server, 184, panel_y + 6, 1, 24, 0x00475569); /* Separator */

    /* Active Taskbar Item */
    int16_t task_x = 194;
    int16_t task_w = 200;
    desktop_fill_rect(server, task_x, panel_y + 3, task_w, 30, 0x000f172a);
    desktop_draw_rect(server, task_x, panel_y + 3, task_w, 30, 0x002563eb);
    desktop_fill_rect(server, task_x, panel_y + panel_h - 2, task_w, 2, 0x003b82f6); /* Active bottom bar */
    desktop_draw_icon_16(server, task_x + 8, panel_y + 10, ICON_FOLDER);
    desktop_draw_text(server, task_x + 28, panel_y + 10, "File Manager", 0x00f1f5f9);

    /* System Tray Area */
    desktop_fill_rect(server, server->width - 150, panel_y + 6, 1, 24, 0x00475569);
    /* Sound Icon */
    desktop_draw_text(server, server->width - 138, panel_y + 10, "[*]", 0x0038bdf8);
    /* Network Icon */
    desktop_draw_text(server, server->width - 105, panel_y + 10, "[#]", 0x0022c55e);
    desktop_fill_rect(server, server->width - 72, panel_y + 6, 1, 24, 0x00475569);
    /* Clock */
    desktop_draw_text(server, server->width - 58, panel_y + 10, "12:00", 0x00ffffff);

    /* 4. Render Open Application Window */
    int16_t win_x = 160;
    int16_t win_y = 60;
    uint16_t win_w = 660;
    uint16_t win_h = 440;

    /* Window Drop Shadow */
    desktop_fill_rect(server, win_x + 6, win_y + win_h, win_w, 4, 0x000f172a);
    desktop_fill_rect(server, win_x + win_w, win_y + 6, 4, win_h, 0x000f172a);

    /* Window Border */
    desktop_draw_rect(server, win_x, win_y, win_w, win_h, 0x002563eb);

    /* Titlebar */
    int16_t tb_h = 28;
    desktop_fill_rect(server, win_x + 1, win_y + 1, win_w - 2, tb_h, 0x002563eb);
    desktop_draw_icon_16(server, win_x + 8, win_y + 6, ICON_FOLDER);
    desktop_draw_text(server, win_x + 28, win_y + 6, "File Manager - /root", 0x00ffffff);

    /* Window Buttons (Minimize, Maximize, Close) */
    int16_t btn_y = win_y + 5;
    desktop_fill_rect(server, win_x + win_w - 68, btn_y, 18, 18, 0x003b82f6);
    desktop_draw_text(server, win_x + win_w - 63, btn_y + 1, "-", 0x00ffffff);

    desktop_fill_rect(server, win_x + win_w - 46, btn_y, 18, 18, 0x003b82f6);
    desktop_draw_text(server, win_x + win_w - 41, btn_y + 1, "+", 0x00ffffff);

    desktop_fill_rect(server, win_x + win_w - 24, btn_y, 18, 18, 0x00dc2626); /* Red close */
    desktop_draw_text(server, win_x + win_w - 19, btn_y + 1, "x", 0x00ffffff);

    /* Menu Bar */
    int16_t menu_y = win_y + tb_h + 1;
    desktop_fill_rect(server, win_x + 1, menu_y, win_w - 2, 22, 0x00f8fafc);
    desktop_fill_rect(server, win_x + 1, menu_y + 21, win_w - 2, 1, 0x00e2e8f0);
    desktop_draw_text(server, win_x + 12, menu_y + 3, "File   Edit   View   Go   Bookmarks   Help", 0x00334155);

    /* Toolbar / Path Bar */
    int16_t tool_y = menu_y + 22;
    desktop_fill_rect(server, win_x + 1, tool_y, win_w - 2, 30, 0x00f1f5f9);
    desktop_fill_rect(server, win_x + 1, tool_y + 29, win_w - 2, 1, 0x00cbd5e1);
    desktop_draw_text(server, win_x + 12, tool_y + 7, "< Back   > Fwd   ^ Up   Home", 0x00475569);
    /* Location Box */
    desktop_fill_rect(server, win_x + 240, tool_y + 4, win_w - 250, 22, 0x00ffffff);
    desktop_draw_rect(server, win_x + 240, tool_y + 4, win_w - 250, 22, 0x00cbd5e1);
    desktop_draw_text(server, win_x + 248, tool_y + 7, "Location: /root", 0x000f172a);

    /* Window Client Area */
    int16_t client_y = tool_y + 30;
    int16_t client_h = win_h - tb_h - 22 - 30 - 24;
    desktop_fill_rect(server, win_x + 1, client_y, win_w - 2, client_h, 0x00ffffff);

    /* Folder and file items in client area */
    struct {
        int x, y, icon;
        const char *name;
    } files[] = {
        { win_x + 30,  client_y + 20, ICON_FOLDER, "Desktop" },
        { win_x + 140, client_y + 20, ICON_FOLDER, "Documents" },
        { win_x + 260, client_y + 20, ICON_FOLDER, "Downloads" },
        { win_x + 380, client_y + 20, ICON_FOLDER, "Pictures" },
        { win_x + 490, client_y + 20, ICON_FOLDER, "Music" },
        { win_x + 30,  client_y + 110, ICON_FILE_TEXT, "notes.txt" },
        { win_x + 140, client_y + 110, ICON_FILE_TEXT, "config.sys" },
        { win_x + 260, client_y + 110, ICON_FILE_TEXT, "akaos.img" },
    };
    for (size_t i = 0; i < sizeof(files)/sizeof(files[0]); ++i) {
        if (files[i].icon == ICON_FOLDER) {
            desktop_draw_icon_32(server, files[i].x + 10, files[i].y, ICON_FOLDER);
        } else {
            desktop_draw_icon_32(server, files[i].x + 10, files[i].y, ICON_COMPUTER);
        }
        desktop_draw_text(server, files[i].x, files[i].y + 36, files[i].name, 0x000f172a);
    }

    /* Status Bar */
    int16_t stat_y = win_y + win_h - 24;
    desktop_fill_rect(server, win_x + 1, stat_y, win_w - 2, 23, 0x00f1f5f9);
    desktop_fill_rect(server, win_x + 1, stat_y, win_w - 2, 1, 0x00e2e8f0);
    desktop_draw_text(server, win_x + 12, stat_y + 4, "8 items (5 folders, 3 files) | 512 MB available", 0x0064748b);

    /* 5. Render Open Start Menu / Application Launcher Popup */
    int16_t menu_w = 210;
    int16_t menu_h = 216;
    int16_t pop_x = 4;
    int16_t pop_y = panel_y - menu_h;
    /* Drop shadow */
    desktop_fill_rect(server, pop_x + 4, pop_y + 4, menu_w, menu_h, 0x000a0f1d);
    /* Body */
    desktop_fill_rect(server, pop_x, pop_y, menu_w, menu_h, 0x001e2430);
    desktop_draw_rect(server, pop_x, pop_y, menu_w, menu_h, 0x003b82f6);

    const char *title_env = is_lxde ? "LXDE Applications" : (is_xfce ? "XFCE Menu" : (is_lxqt ? "LXQt Applications" : "Openbox Menu"));
    desktop_fill_rect(server, pop_x + 1, pop_y + 1, menu_w - 2, 24, 0x002d3748);
    desktop_draw_text(server, pop_x + 8, pop_y + 5, title_env, 0x0060a5fa);

    const char *menu_items[] = {
        "Accessories     >",
        "Graphics        >",
        "Internet        >",
        "Office          >",
        "System Tools    >",
        "Preferences     >",
        "-----------------",
        "Run Command...",
        "Log Out...",
    };
    for (size_t i = 0; i < sizeof(menu_items)/sizeof(menu_items[0]); ++i) {
        int16_t item_y = pop_y + 28 + (int16_t)i * 20;
        if (i == 6) {
            desktop_fill_rect(server, pop_x + 6, item_y + 9, menu_w - 12, 1, 0x00475569);
            continue;
        }
        uint32_t item_color = (i == 8) ? 0x00f87171 : 0x00f1f5f9;
        desktop_draw_text(server, pop_x + 12, item_y + 2, menu_items[i], item_color);
    }
}
