/*
 * akaOS Native Professional Desktop Environment (LXDE / LXQt)
 *
 * Full-screen interactive desktop shell featuring:
 * - Full-screen gradient wallpaper with official desktop watermark
 * - Interactive desktop shortcut icons (Terminal, Files, Editor, Taskmgr, Web, Settings, Palette)
 * - Interactive bottom panel with Start Menu, Quick Launch, Window Taskbar, and System Tray
 * - Real-time digital clock and status indicators
 * - Pop-up Application Launcher Menu
 * - D-Bus companion service registration (Openbox, PCManFM, LXQt Panel)
 */

#include <akaos/dbus.h>
#include <akaos/display.h>
#include <akaos/ui_font_aa.h>
#include <akaos/toolkit.h>
#include <akaos/account.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define PANEL_HEIGHT 38U
#define SURFACE_NAME_SIZE 32U
#define PROFILE_COMPONENT_CAPACITY 2U

struct compat_surface {
    int fd;
    size_t size;
    char name[SURFACE_NAME_SIZE];
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static volatile int session_stop_requested;
static pid_t session_component_pids[PROFILE_COMPONENT_CAPACITY];
static size_t session_component_count;
static char registered_service_name[64];
static int component_service_registered;
static int component_name_descriptor = -1;

static void session_signal_handler(int signal_number) {
    (void)signal_number;
    session_stop_requested = 1;
}

static void stop_session_components(void) {
    for (size_t index = 0; index < session_component_count; ++index) {
        if (session_component_pids[index] > 0)
            (void)kill(session_component_pids[index], SIGTERM);
    }
    for (size_t index = 0; index < session_component_count; ++index) {
        if (session_component_pids[index] <= 0) continue;
        int status = 0;
        (void)waitpid(session_component_pids[index], &status, 0);
        session_component_pids[index] = -1;
    }
    session_component_count = 0U;
}

enum profile_kind {
    PROFILE_LXDE = 0,
    PROFILE_LXQT,
    PROFILE_OPENBOX,
    PROFILE_XFCE,
    PROFILE_GNOME,
    PROFILE_KDE
};

static enum profile_kind profile_for_name(const char *name) {
    if (!name) return PROFILE_LXDE;
    if (strstr(name, "lxqt") || strstr(name, "pcmanfm-qt")) return PROFILE_LXQT;
    if (strstr(name, "openbox")) return PROFILE_OPENBOX;
    if (strstr(name, "xfce")) return PROFILE_XFCE;
    if (strstr(name, "gnome") || strstr(name, "mutter")) return PROFILE_GNOME;
    if (strstr(name, "plasma") || strstr(name, "kwin")) return PROFILE_KDE;
    return PROFILE_LXDE;
}

static const char *profile_id(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXQT: return "lxqt";
    case PROFILE_OPENBOX: return "openbox";
    case PROFILE_XFCE: return "xfce";
    case PROFILE_GNOME: return "gnome";
    case PROFILE_KDE: return "kde";
    default: return "lxde";
    }
}

static const char *profile_label(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXQT: return "LXQt";
    case PROFILE_OPENBOX: return "Openbox";
    case PROFILE_XFCE: return "XFCE";
    case PROFILE_GNOME: return "GNOME";
    case PROFILE_KDE: return "KDE Plasma";
    default: return "LXDE";
    }
}

static const char *profile_subtitle(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXQT: return "Lightweight Qt6 Desktop Environment";
    case PROFILE_OPENBOX: return "Window Manager Session";
    case PROFILE_XFCE: return "XFCE 4.18 Desktop Session";
    case PROFILE_GNOME: return "GTK and GIO-compatible session";
    case PROFILE_KDE: return "Qt and KWin-compatible session";
    default: return "Lightweight X11 Desktop Environment";
    }
}

static const char *profile_icon(enum profile_kind profile) {
    return profile_id(profile);
}

static size_t profile_component_count(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXDE: return 2U;       /* Openbox + PCManFM */
    case PROFILE_LXQT: return 2U;       /* LXQt panel + PCManFM-Qt */
    case PROFILE_GNOME: return 2U;      /* Mutter + GNOME session */
    case PROFILE_KDE: return 2U;        /* KWin + Plasma session */
    default: return 0U;
    }
}

static const char *profile_component(enum profile_kind profile, size_t index) {
    switch (profile) {
    case PROFILE_LXDE:
        if (index == 0U) return "/bin/openbox";
        if (index == 1U) return "/bin/pcmanfm";
        break;
    case PROFILE_LXQT:
        if (index == 0U) return "/bin/lxqt-panel";
        if (index == 1U) return "/bin/pcmanfm-qt";
        break;
    case PROFILE_GNOME:
        if (index == 0U) return "/bin/mutter";
        if (index == 1U) return "/bin/gnome-session";
        break;
    case PROFILE_KDE:
        if (index == 0U) return "/bin/kwin";
        if (index == 1U) return "/bin/startplasma";
        break;
    default:
        break;
    }
    return NULL;
}

enum component_role {
    COMPONENT_NONE = 0,
    COMPONENT_OPENBOX,
    COMPONENT_PCMANFM,
    COMPONENT_LXQT_PANEL,
    COMPONENT_PCMANFM_QT,
    COMPONENT_MUTTER,
    COMPONENT_GNOME_SESSION,
    COMPONENT_KWIN,
    COMPONENT_PLASMA
};

static enum component_role role_for_name(const char *name) {
    if (!name) return COMPONENT_NONE;
    if (strstr(name, "openbox")) return COMPONENT_OPENBOX;
    if (strstr(name, "pcmanfm-qt")) return COMPONENT_PCMANFM_QT;
    if (strstr(name, "pcmanfm")) return COMPONENT_PCMANFM;
    if (strstr(name, "lxqt-panel")) return COMPONENT_LXQT_PANEL;
    if (strstr(name, "mutter")) return COMPONENT_MUTTER;
    if (strstr(name, "gnome-session")) return COMPONENT_GNOME_SESSION;
    if (strstr(name, "kwin")) return COMPONENT_KWIN;
    if (strstr(name, "plasma") || strstr(name, "startplasma")) return COMPONENT_PLASMA;
    return COMPONENT_NONE;
}

static const char *component_service_name(enum component_role role) {
    switch (role) {
    case COMPONENT_OPENBOX: return "org.akaos.LXDE.Openbox";
    case COMPONENT_PCMANFM: return "org.akaos.LXDE.PCManFM";
    case COMPONENT_LXQT_PANEL: return "org.akaos.LXQt.Panel";
    case COMPONENT_PCMANFM_QT: return "org.akaos.LXQt.PCManFM";
    case COMPONENT_MUTTER: return "org.akaos.GNOME.Mutter";
    case COMPONENT_GNOME_SESSION: return "org.akaos.GNOME.Session";
    case COMPONENT_KWIN: return "org.akaos.KDE.KWin";
    case COMPONENT_PLASMA: return "org.akaos.KDE.Plasma";
    default: return NULL;
    }
}

static const char *component_executable(enum component_role role) {
    switch (role) {
    case COMPONENT_OPENBOX: return "/bin/openbox";
    case COMPONENT_PCMANFM: return "/bin/pcmanfm";
    case COMPONENT_LXQT_PANEL: return "/bin/lxqt-panel";
    case COMPONENT_PCMANFM_QT: return "/bin/pcmanfm-qt";
    case COMPONENT_MUTTER: return "/bin/mutter";
    case COMPONENT_GNOME_SESSION: return "/bin/gnome-session";
    case COMPONENT_KWIN: return "/bin/kwin";
    case COMPONENT_PLASMA: return "/bin/startplasma";
    default: return NULL;
    }
}

static void unregister_component_service(void) {
    if (registered_service_name[0] == '\0') return;
    char service_name[64];
    strncpy(service_name, registered_service_name, sizeof(service_name) - 1U);
    service_name[sizeof(service_name) - 1U] = '\0';
    if (component_name_descriptor >= 0) {
        (void)akaos_dbus_release_name(component_name_descriptor, service_name);
        component_name_descriptor = -1;
    }
    if (component_service_registered) {
        (void)akaos_dbus_unregister_service(service_name);
        component_service_registered = 0;
    }
    registered_service_name[0] = '\0';
}

static int register_component_service(const char *name) {
    enum component_role role = role_for_name(name);
    const char *service = component_service_name(role);
    const char *executable = component_executable(role);
    if (!service || !executable) return 0;
    int metadata_result = akaos_dbus_register_service(
        service, executable, "--component", 0, 1000U);
    int metadata_owned = metadata_result == 0;
    if (metadata_owned) {
        strncpy(registered_service_name, service, sizeof(registered_service_name) - 1U);
        registered_service_name[sizeof(registered_service_name) - 1U] = '\0';
        component_service_registered = 1;
        printf("%s D-Bus activation service registered (%s)\n",
               name ? name : "desktop", service);
    }
    int descriptor = akaos_dbus_claim_name(service);
    if (descriptor >= 0) {
        strncpy(registered_service_name, service, sizeof(registered_service_name) - 1U);
        registered_service_name[sizeof(registered_service_name) - 1U] = '\0';
        component_name_descriptor = descriptor;
        printf("%s D-Bus well-known name claimed (%s)\n",
               name ? name : "desktop", service);
        (void)akaos_dbus_emit_desktop_properties_changed(descriptor, "ready");
    }
    return 0;
}

static void run_component_service_loop(const char *name) {
    printf("%s: D-Bus service loop active\n", name ? name : "desktop");
    while (!session_stop_requested) {
        (void)poll(NULL, 0, 1000);
    }
}

/* --- Graphics Primitives --- */

static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint32_t inv = 255U - alpha;
    uint32_t r = (((bg >> 16) & 0xffU) * inv + ((fg >> 16) & 0xffU) * alpha + 127U) / 255U;
    uint32_t g = (((bg >> 8) & 0xffU) * inv + ((fg >> 8) & 0xffU) * alpha + 127U) / 255U;
    uint32_t b = ((bg & 0xffU) * inv + (fg & 0xffU) * alpha + 127U) / 255U;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void fill_rect(uint32_t *pixels, uint32_t width, uint32_t height,
                      int x, int y, int rw, int rh, uint32_t color) {
    if (!pixels || rw <= 0 || rh <= 0) return;
    for (int py = y; py < y + rh; ++py) {
        if (py < 0 || py >= (int)height) continue;
        for (int px = x; px < x + rw; ++px) {
            if (px >= 0 && px < (int)width) {
                uint8_t a = (color >> 24) & 0xffU;
                if (a == 0xffU || a == 0) {
                    pixels[(size_t)py * width + (size_t)px] = color | 0xFF000000;
                } else {
                    size_t off = (size_t)py * width + (size_t)px;
                    pixels[off] = alpha_blend(pixels[off], color, a);
                }
            }
        }
    }
}

static void fill_rounded_rect(uint32_t *pixels, uint32_t width, uint32_t height,
                              int x, int y, int rw, int rh, int radius,
                              uint32_t fill, uint32_t border) {
    if (!pixels || rw <= 0 || rh <= 0 || radius < 0) return;
    for (int py = y; py < y + rh; ++py) {
        if (py < 0 || py >= (int)height) continue;
        for (int px = x; px < x + rw; ++px) {
            if (px < 0 || px >= (int)width) continue;
            int lx = px - x;
            int ly = py - y;
            int cx = lx < radius ? radius - lx : lx >= rw - radius ? lx - (rw - radius - 1) : 0;
            int cy = ly < radius ? radius - ly : ly >= rh - radius ? ly - (rh - radius - 1) : 0;
            if (cx > 0 && cy > 0 && cx * cx + cy * cy > radius * radius) continue;
            int is_edge = (lx == 0 || ly == 0 || lx == rw - 1 || ly == rh - 1);
            uint32_t c = (is_edge && border) ? border : fill;
            size_t off = (size_t)py * width + (size_t)px;
            uint8_t a = (c >> 24) & 0xffU;
            if (a == 0xffU || a == 0) pixels[off] = c | 0xFF000000;
            else pixels[off] = alpha_blend(pixels[off], c, a);
        }
    }
}

static void draw_text(uint32_t *pixels, uint32_t width, uint32_t height,
                      int x, int y, const char *text, uint32_t color) {
    if (!pixels || !text) return;
    int origin = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        unsigned char ch = (unsigned char)text[index];
        for (uint32_t row = 0; row < AKAOS_UI_FONT_HEIGHT; ++row) {
            for (uint32_t col = 0; col < AKAOS_UI_FONT_WIDTH; ++col) {
                uint8_t alpha = akaos_ui_font_get_alpha(ch, row, col);
                if (!alpha) continue;
                int px = origin + (int)col;
                int py = y + (int)row;
                if (px < 0 || py < 0 || px >= (int)width || py >= (int)height) continue;
                size_t off = (size_t)py * width + (size_t)px;
                pixels[off] = alpha_blend(pixels[off], color, alpha);
            }
        }
        origin += (int)akaos_ui_font_get_advance(ch);
    }
}

/* App Icon Glyph Drawing */
static void draw_app_icon(uint32_t *pixels, uint32_t width, uint32_t height,
                          int x, int y, int icon_type) {
    fill_rounded_rect(pixels, width, height, x, y, 36, 36, 9,
                      UINT32_C(0xFF1E293B), UINT32_C(0xFF475569));
    switch (icon_type) {
    case 0: /* Files / PCManFM (Folder icon) */
        fill_rounded_rect(pixels, width, height, x + 6, y + 8, 12, 6, 2,
                          UINT32_C(0xFFF59E0B), UINT32_C(0xFFD97706));
        fill_rounded_rect(pixels, width, height, x + 6, y + 12, 24, 16, 3,
                          UINT32_C(0xFFFBBF24), UINT32_C(0xFFD97706));
        break;
    case 1: /* Terminal */
        fill_rounded_rect(pixels, width, height, x + 5, y + 6, 26, 24, 4,
                          UINT32_C(0xFF0F172A), UINT32_C(0xFF334155));
        draw_text(pixels, width, height, x + 9, y + 10, ">_", UINT32_C(0xFF10B981));
        break;
    case 2: /* Notepad / Text Editor */
        fill_rounded_rect(pixels, width, height, x + 9, y + 6, 18, 24, 3,
                          UINT32_C(0xFFF8FAFC), UINT32_C(0xFFCBD5E1));
        fill_rect(pixels, width, height, x + 13, y + 12, 10, 2, UINT32_C(0xFF3B82F6));
        fill_rect(pixels, width, height, x + 13, y + 17, 10, 2, UINT32_C(0xFF94A3B8));
        fill_rect(pixels, width, height, x + 13, y + 22, 8, 2, UINT32_C(0xFF94A3B8));
        break;
    case 3: /* Task Manager */
        fill_rect(pixels, width, height, x + 8, y + 20, 4, 9, UINT32_C(0xFF60A5FA));
        fill_rect(pixels, width, height, x + 15, y + 13, 4, 16, UINT32_C(0xFF34D399));
        fill_rect(pixels, width, height, x + 22, y + 8, 4, 21, UINT32_C(0xFFA78BFA));
        break;
    case 4: /* Web Browser */
        fill_rounded_rect(pixels, width, height, x + 6, y + 6, 24, 24, 12,
                          UINT32_C(0xFF3B82F6), UINT32_C(0xFF60A5FA));
        fill_rect(pixels, width, height, x + 8, y + 17, 20, 2, UINT32_C(0xFFFFFFFF));
        fill_rect(pixels, width, height, x + 17, y + 8, 2, 20, UINT32_C(0xFFFFFFFF));
        break;
    case 5: /* Settings */
        fill_rounded_rect(pixels, width, height, x + 7, y + 7, 22, 22, 11,
                          UINT32_C(0xFF64748B), UINT32_C(0xFF94A3B8));
        fill_rounded_rect(pixels, width, height, x + 13, y + 13, 10, 10, 5,
                          UINT32_C(0xFF1E293B), UINT32_C(0xFF94A3B8));
        break;
    case 6: /* Palette / Appearance */
        fill_rounded_rect(pixels, width, height, x + 7, y + 7, 10, 10, 3,
                          UINT32_C(0xFFEC4899), 0);
        fill_rounded_rect(pixels, width, height, x + 19, y + 7, 10, 10, 3,
                          UINT32_C(0xFFF59E0B), 0);
        fill_rounded_rect(pixels, width, height, x + 7, y + 19, 10, 10, 3,
                          UINT32_C(0xFF10B981), 0);
        fill_rounded_rect(pixels, width, height, x + 19, y + 19, 10, 10, 3,
                          UINT32_C(0xFF3B82F6), 0);
        break;
    default:
        fill_rounded_rect(pixels, width, height, x + 8, y + 8, 20, 20, 5,
                          UINT32_C(0xFF38BDF8), UINT32_C(0xFF7DD3FC));
        break;
    }
}

/* Desktop shortcut items */
struct desktop_shortcut {
    const char *label;
    const char *exec;
    int icon_type;
    int x, y, w, h;
};

static struct desktop_shortcut SHORTCUTS[] = {
    {"Files", "/bin/pcmanfm", 0, 24, 24, 72, 70},
    {"Terminal", "/bin/terminal", 1, 24, 106, 72, 70},
    {"Editor", "/bin/notepad", 2, 24, 188, 72, 70},
    {"Taskmgr", "/bin/taskmgr", 3, 24, 270, 72, 70},
    {"Browser", "/bin/webviewer", 4, 24, 352, 72, 70},
    {"Settings", "/bin/akaos-config", 5, 24, 434, 72, 70},
    {"Palette", "/bin/palette", 6, 24, 516, 72, 70},
};
#define SHORTCUT_COUNT (sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0]))

struct menu_entry {
    const char *name;
    const char *desc;
    const char *exec;
    int icon_type;
};

static struct menu_entry MENU_ENTRIES[] = {
    {"Terminal", "System Command Shell", "/bin/terminal", 1},
    {"File Manager", "Browse files and folders", "/bin/pcmanfm", 0},
    {"Text Editor", "Edit text and documents", "/bin/notepad", 2},
    {"Task Manager", "Monitor processes and memory", "/bin/taskmgr", 3},
    {"Web Browser", "Explore local and web pages", "/bin/webviewer", 4},
    {"Palette", "View colors and theme preview", "/bin/palette", 6},
    {"System Settings", "Configure akaOS options", "/bin/akaos-config", 5},
    {"Restart System", "Reboot akaOS safely", "/bin/reboot", 7},
    {"Shut Down", "Power off computer", "/bin/poweroff", 8},
};
#define MENU_ENTRY_COUNT (sizeof(MENU_ENTRIES) / sizeof(MENU_ENTRIES[0]))

/* Desktop state */
struct desktop_state {
    int menu_open;
    int hovered_menu_item;
    int hovered_shortcut;
    int hovered_start;
    int hovered_quick[4];
    int hovered_task;
    int clock_hours;
    int clock_minutes;
    int clock_seconds;
};

static void launch_app(const char *executable) {
    if (!executable) return;
    char path[64];
    strncpy(path, executable, sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    char *args[] = {path, NULL};
    pid_t pid = spawn(path, args);
    if (pid > 0) {
        printf("[desktop] Launched %s (PID=%d)\n", path, (int)pid);
    }
}

/* Render Desktop Scene */
static void render_full_desktop(uint32_t *pixels, uint32_t width, uint32_t height,
                                struct akaos_compositor_channel *channel,
                                enum profile_kind profile,
                                struct desktop_state *st) {
    /* 1. Full-screen elegant Twilight Slate Wallpaper */
    for (uint32_t y = 0; y < height - PANEL_HEIGHT; ++y) {
        uint32_t r = 14U + (y * 14U) / height;
        uint32_t g = 20U + (y * 22U) / height;
        uint32_t b = 34U + (y * 36U) / height;
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < width; ++x) {
            pixels[y * width + x] = color;
        }
    }

    /* Desktop Branding Watermark in Bottom-Right */
    const char *title = profile_label(profile);
    const char *sub = profile_subtitle(profile);
    int mark_x = (int)width - 320;
    int mark_y = (int)height - (int)PANEL_HEIGHT - 60;
    draw_text(pixels, width, height, mark_x, mark_y, title, UINT32_C(0x80F8FAFC));
    draw_text(pixels, width, height, mark_x, mark_y + 24, sub, UINT32_C(0x5094A3B8));

    /* 2. Desktop Shortcut Icons (Left Column) */
    for (size_t i = 0; i < SHORTCUT_COUNT; ++i) {
        struct desktop_shortcut *sc = &SHORTCUTS[i];
        if (sc->y + sc->h >= (int)(height - PANEL_HEIGHT)) continue;

        /* Selection/Hover highlight */
        if ((int)i == st->hovered_shortcut) {
            fill_rounded_rect(pixels, width, height, sc->x - 4, sc->y - 4,
                              sc->w + 8, sc->h + 8, 8,
                              UINT32_C(0x3538BDF8), UINT32_C(0x8038BDF8));
        }

        /* Icon */
        draw_app_icon(pixels, width, height, sc->x + 18, sc->y + 4, sc->icon_type);

        /* Label with drop shadow */
        int label_w = (int)strlen(sc->label) * 8;
        int lx = sc->x + (sc->w - label_w) / 2;
        draw_text(pixels, width, height, lx + 1, sc->y + 45, sc->label, UINT32_C(0xFF000000));
        draw_text(pixels, width, height, lx, sc->y + 44, sc->label, UINT32_C(0xFFF8FAFC));
    }

    /* 3. Bottom Panel (LXPanel style: 38px height) */
    int panel_y = (int)(height - PANEL_HEIGHT);
    fill_rect(pixels, width, height, 0, panel_y, (int)width, (int)PANEL_HEIGHT,
              UINT32_C(0xFF0F172A));
    fill_rect(pixels, width, height, 0, panel_y, (int)width, 1,
              UINT32_C(0x6038BDF8)); /* Top specular border */

    /* Start Menu Button (Left) */
    int start_w = 88;
    uint32_t start_fill = (st->menu_open || st->hovered_start)
                              ? UINT32_C(0xFF0284C7) : UINT32_C(0xFF1D4ED8);
    fill_rounded_rect(pixels, width, height, 4, panel_y + 4, start_w, 30, 6,
                      start_fill, UINT32_C(0xFF60A5FA));

    /* Start emblem: modern quadrant */
    int em_x = 10;
    int em_y = panel_y + 11;
    fill_rect(pixels, width, height, em_x, em_y, 6, 6, UINT32_C(0xFF38BDF8));
    fill_rect(pixels, width, height, em_x + 8, em_y, 6, 6, UINT32_C(0xFF34D399));
    fill_rect(pixels, width, height, em_x, em_y + 8, 6, 6, UINT32_C(0xFFFBBF24));
    fill_rect(pixels, width, height, em_x + 8, em_y + 8, 6, 6, UINT32_C(0xFFA855F7));
    draw_text(pixels, width, height, em_x + 20, panel_y + 11, title, UINT32_C(0xFFFFFFFF));

    /* Quick Launch Icons */
    int qx = 102;
    int q_types[4] = {1, 0, 2, 4};
    for (int q = 0; q < 4; ++q) {
        if (st->hovered_quick[q]) {
            fill_rounded_rect(pixels, width, height, qx - 2, panel_y + 3, 30, 30, 6,
                              UINT32_C(0x3538BDF8), UINT32_C(0x6038BDF8));
        }
        draw_app_icon(pixels, width, height, qx, panel_y + 5, q_types[q]);
        qx += 34;
    }

    /* Window Taskbar Items (Middle) */
    int task_x = qx + 12;
    int max_task_x = (int)width - 240;
    if (channel) {
        for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
            const struct akaos_compositor_slot *s = &channel->surfaces[i];
            if (!s->active || (s->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL)))
                continue;
            if (task_x + 130 > max_task_x) break;

            int focused = (s->surface_id == channel->active_surface_id);
            uint32_t t_fill = focused ? UINT32_C(0xFF1E3A5F) : UINT32_C(0xFF1E293B);
            uint32_t t_border = focused ? UINT32_C(0xFF38BDF8) : UINT32_C(0xFF475569);

            fill_rounded_rect(pixels, width, height, task_x, panel_y + 4, 126, 30, 6,
                              t_fill, t_border);

            const char *w_title = s->title[0] ? s->title : "Window";
            draw_text(pixels, width, height, task_x + 10, panel_y + 11, w_title,
                      focused ? UINT32_C(0xFFF8FAFC) : UINT32_C(0xFFCBD5E1));

            if (focused) {
                fill_rect(pixels, width, height, task_x + 6, panel_y + 31, 114, 2,
                          UINT32_C(0xFF38BDF8));
            }

            task_x += 132;
        }
    }

    /* System Tray (Right) */
    int tray_x = (int)width - 230;
    fill_rounded_rect(pixels, width, height, tray_x, panel_y + 4, 224, 30, 6,
                      UINT32_C(0xFF1E293B), UINT32_C(0xFF334155));

    /* Network green dot */
    fill_rounded_rect(pixels, width, height, tray_x + 10, panel_y + 13, 10, 10, 5,
                      UINT32_C(0xFF10B981), UINT32_C(0xFF34D399));
    draw_text(pixels, width, height, tray_x + 24, panel_y + 11, "LAN", UINT32_C(0xFF94A3B8));

    /* Clock & Date */
    char clock_str[16];
    snprintf(clock_str, sizeof(clock_str), "%02d:%02d:%02d",
             st->clock_hours, st->clock_minutes, st->clock_seconds);
    draw_text(pixels, width, height, tray_x + 65, panel_y + 11, clock_str,
              UINT32_C(0xFFF8FAFC));

    /* Power / Logout button */
    int pwr_x = (int)width - 32;
    fill_rounded_rect(pixels, width, height, pwr_x, panel_y + 6, 24, 24, 5,
                      UINT32_C(0xFFDC2626), UINT32_C(0xFFEF4444));
    draw_text(pixels, width, height, pwr_x + 8, panel_y + 10, "X", UINT32_C(0xFFFFFFFF));

    /* 4. Pop-up Application Launcher Menu */
    if (st->menu_open) {
        int menu_w = 260;
        int menu_h = (int)MENU_ENTRY_COUNT * 32 + 40;
        int menu_x = 4;
        int menu_y = panel_y - menu_h - 4;

        /* Ambient Drop Shadow */
        fill_rounded_rect(pixels, width, height, menu_x + 4, menu_y + 4,
                          menu_w, menu_h, 12,
                          UINT32_C(0x50000000), 0);

        /* Acrylic Menu Container */
        fill_rounded_rect(pixels, width, height, menu_x, menu_y, menu_w, menu_h, 10,
                          UINT32_C(0xF80F172A), UINT32_C(0xFF38BDF8));

        /* Header */
        char menu_hdr[48];
        snprintf(menu_hdr, sizeof(menu_hdr), "%s Applications", title);
        draw_text(pixels, width, height, menu_x + 14, menu_y + 12,
                  menu_hdr, UINT32_C(0xFF38BDF8));
        fill_rect(pixels, width, height, menu_x + 10, menu_y + 32, menu_w - 20, 1,
                  UINT32_C(0xFF334155));

        /* Menu Items */
        for (size_t m = 0; m < MENU_ENTRY_COUNT; ++m) {
            int item_y = menu_y + 38 + (int)m * 32;
            if ((int)m == st->hovered_menu_item) {
                fill_rounded_rect(pixels, width, height, menu_x + 6, item_y,
                                  menu_w - 12, 28, 6,
                                  UINT32_C(0xFF2563EB), UINT32_C(0xFF60A5FA));
            }
            draw_app_icon(pixels, width, height, menu_x + 10, item_y + 2,
                          MENU_ENTRIES[m].icon_type);
            draw_text(pixels, width, height, menu_x + 44, item_y + 8,
                      MENU_ENTRIES[m].name, UINT32_C(0xFFF8FAFC));
        }
    }
}

static struct compat_surface create_surface(const char *name, uint32_t width,
                                            uint32_t height) {
    struct compat_surface surface;
    memset(&surface, 0, sizeof(surface));
    surface.fd = -1;
    if (!name || width == 0 || height == 0) return surface;
    strncpy(surface.name, name, sizeof(surface.name) - 1U);
    size_t content = sizeof(struct akaos_surface_header) +
                     (size_t)width * height * sizeof(uint32_t);
    surface.size = (content + 4095U) & ~(size_t)4095U;
    surface.fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (surface.fd < 0 && errno == EEXIST) {
        (void)shm_unlink(name);
        surface.fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    }
    if (surface.fd < 0 || ftruncate(surface.fd, (off_t)surface.size) != 0)
        goto failed;
    surface.header = mmap(NULL, surface.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, surface.fd, 0);
    if (surface.header == MAP_FAILED) {
        surface.header = NULL;
        goto failed;
    }
    surface.header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    surface.header->width = width;
    surface.header->height = height;
    surface.header->stride_pixels = width;
    surface.header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    surface.header->pixel_offset = sizeof(struct akaos_surface_header);
    surface.header->byte_size = surface.size;
    surface.pixels = (uint32_t *)((uint8_t *)surface.header +
                                  surface.header->pixel_offset);
    return surface;
failed:
    if (surface.header) munmap(surface.header, surface.size);
    if (surface.fd >= 0) close(surface.fd);
    surface.fd = -1;
    surface.header = NULL;
    (void)shm_unlink(surface.name);
    return surface;
}

static void destroy_surface(struct compat_surface *surface) {
    if (!surface) return;
    if (surface->header) {
        (void)munmap(surface->header, surface->size);
        surface->header = NULL;
    }
    if (surface->fd >= 0) {
        close(surface->fd);
        surface->fd = -1;
    }
    if (surface->name[0]) (void)shm_unlink(surface->name);
}

static int register_desktop_surface(struct akaos_compositor_channel *channel,
                                     uint32_t surface_id, uint32_t width,
                                     uint32_t height, int32_t x, int32_t y,
                                     const char *name,
                                     enum profile_kind profile) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_ATTACH;
    message.flags = AKAOS_SURFACE_FLAG_DESKTOP;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.z_order = 0; /* Background layer */
    strncpy(message.shm_name, name, sizeof(message.shm_name) - 1U);
    strncpy(message.title, profile_label(profile), sizeof(message.title) - 1U);
    strncpy(message.icon_name, profile_icon(profile), sizeof(message.icon_name) - 1U);
    return akaos_compositor_submit(channel, &message);
}

static int send_damage(struct akaos_compositor_channel *channel,
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

static void send_destroy(struct akaos_compositor_channel *channel,
                         uint32_t surface_id) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DESTROY;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    (void)akaos_compositor_submit(channel, &message);
}

static int display_protocol_marker_ready(void) {
    int fd = shm_open("/akaos-display-protocols", O_RDONLY, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static void ensure_display_protocol_bridge(void) {
    if (display_protocol_marker_ready()) return;
    char path[] = "/bin/display-protocols";
    char mode[] = "--both";
    char *arguments[] = {path, mode, NULL};
    if (spawn(path, arguments) < 0) return;
    for (int retry = 0; retry < 40; ++retry) {
        (void)poll(NULL, 0, 25);
        if (display_protocol_marker_ready()) return;
    }
}

static size_t start_session_components(enum profile_kind profile, int once,
                                        pid_t *pids, size_t capacity) {
    size_t started = 0U;
    size_t count = profile_component_count(profile);
    for (size_t index = 0U; index < count; ++index) {
        const char *component = profile_component(profile, index);
        if (!component) continue;
        char component_path[64];
        strncpy(component_path, component, sizeof(component_path) - 1U);
        component_path[sizeof(component_path) - 1U] = '\0';
        char component_mode[] = "--component";
        char once_mode[] = "--once";
        char *arguments[] = {component_path, component_mode,
                             once ? once_mode : NULL, NULL};
        pid_t pid = spawn(component_path, arguments);
        if (pid < 0) continue;
        if (pids && started < capacity) pids[started] = pid;
        ++started;
        (void)poll(NULL, 0, 100);
    }
    return started;
}

static int run_check(enum profile_kind profile) {
    struct akaos_display_info info;
    int bus = akaos_dbus_connect(0);
    int display_ok = akaos_toolkit_display_info(&info) == 0;
    int bus_ok = bus >= 0;
    if (bus_ok) {
        (void)akaos_dbus_ping(bus);
        (void)akaos_dbus_close(bus);
    }
    printf("%s runtime readiness: %s\n", profile_label(profile),
           display_ok && bus_ok ? "READY" : "NOT READY");
    return (display_ok && bus_ok) ? 0 : 1;
}

static const char *program_name(const char *path) {
    if (!path) return "desktop";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int main(int argc, char **argv) {
    const char *process_name = program_name(argc > 0 ? argv[0] : NULL);
    enum profile_kind profile = profile_for_name(process_name);
    int once = 0;
    int check = 0;
    int session_mode = 0;
    int component_mode = 0;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            printf("Usage: %s [--check] [--once] [--session] [--component]\n"
                   "akaOS Native Desktop Environment (%s).\n",
                   process_name, profile_label(profile));
            return 0;
        }
        if (strcmp(argv[index], "--check") == 0) check = 1;
        if (strcmp(argv[index], "--once") == 0) once = 1;
        if (strcmp(argv[index], "--session") == 0) session_mode = 1;
        if (strcmp(argv[index], "--component") == 0) component_mode = 1;
    }
    if (check) return run_check(profile);

    (void)signal(SIGTERM, session_signal_handler);
    (void)signal(SIGINT, session_signal_handler);

    /* Companion component mode: Register D-Bus and run service loop */
    if (component_mode && !once) {
        if (register_component_service(process_name) == 0) {
            run_component_service_loop(process_name);
            unregister_component_service();
            return 0;
        }
    }

    struct akaos_compositor_channel *channel = NULL;
    if (akaos_toolkit_connect(&channel) != 0) {
        printf("%s: compositor is unavailable (errno=%d)\n",
               profile_label(profile), errno);
        return 1;
    }

    struct akaos_display_info display;
    if (akaos_toolkit_display_info(&display) != 0 || display.width == 0 ||
        display.height <= PANEL_HEIGHT) {
        printf("%s: display is unavailable\n", profile_label(profile));
        (void)akaos_toolkit_disconnect(channel);
        return 2;
    }

    ensure_display_protocol_bridge();

    int bus = akaos_dbus_connect(0);
    if (bus >= 0) {
        (void)akaos_dbus_ping(bus);
        (void)akaos_dbus_close(bus);
    }

    /* Start background companion D-Bus services */
    if (session_mode && !component_mode) {
        session_component_count = start_session_components(
            profile, once, session_component_pids, PROFILE_COMPONENT_CAPACITY);
    }

    /* Create FULL-SCREEN Desktop Surface covering the entire screen */
    char name[SURFACE_NAME_SIZE];
    snprintf(name, sizeof(name), "/%s-desktop-%d", profile_id(profile), (int)getpid());
    uint32_t surface_width = display.width;
    uint32_t surface_height = display.height;

    struct compat_surface surface = create_surface(name, surface_width, surface_height);
    if (!surface.header) {
        printf("%s: unable to create full-screen surface (errno=%d)\n",
               profile_label(profile), errno);
        stop_session_components();
        (void)akaos_toolkit_disconnect(channel);
        return 4;
    }

    uint32_t surface_id = (uint32_t)getpid() * 17U + 9U;
    if (!register_desktop_surface(channel, surface_id, surface_width,
                                  surface_height, 0, 0, name, profile) ||
        !send_damage(channel, surface_id, surface_width, surface_height)) {
        printf("%s: compositor rejected desktop surface (errno=%d)\n",
               profile_label(profile), errno);
        destroy_surface(&surface);
        stop_session_components();
        (void)akaos_toolkit_disconnect(channel);
        return 5;
    }

    printf("%s: Full-screen interactive desktop active (%ux%u)\n",
           profile_label(profile), surface_width, surface_height);

    /* Initialize Desktop State */
    struct desktop_state st;
    memset(&st, 0, sizeof(st));
    st.hovered_menu_item = -1;
    st.hovered_shortcut = -1;
    st.clock_hours = 12;
    st.clock_minutes = 0;
    st.clock_seconds = 0;

    render_full_desktop(surface.pixels, surface_width, surface_height,
                        channel, profile, &st);
    send_damage(channel, surface_id, surface_width, surface_height);

    /* Interactive Desktop Event Loop */
    int loop_tick = 0;
    while (!session_stop_requested) {
        int need_redraw = 0;

        /* 1. Poll for input events from the compositor */
        struct akaos_shell_event event;
        while (akaos_shell_event_receive(channel, &event)) {
            if (event.type == AKAOS_SHELL_EVENT_BUTTON && event.code == 0x110 /* BTN_LEFT */ && event.value == 1) {
                int px = event.pointer_x;
                int py = event.pointer_y;
                int panel_y = (int)(surface_height - PANEL_HEIGHT);

                /* A. Check Start Menu Button Click */
                if (px >= 4 && px <= 92 && py >= panel_y + 4 && py <= panel_y + 34) {
                    st.menu_open = !st.menu_open;
                    need_redraw = 1;
                    continue;
                }

                /* B. Check Start Menu Items Click */
                if (st.menu_open) {
                    int menu_w = 260;
                    int menu_h = (int)MENU_ENTRY_COUNT * 32 + 40;
                    int menu_x = 4;
                    int menu_y = panel_y - menu_h - 4;

                    if (px >= menu_x && px <= menu_x + menu_w &&
                        py >= menu_y && py <= menu_y + menu_h) {
                        for (size_t m = 0; m < MENU_ENTRY_COUNT; ++m) {
                            int item_y = menu_y + 38 + (int)m * 32;
                            if (py >= item_y && py <= item_y + 28) {
                                launch_app(MENU_ENTRIES[m].exec);
                                st.menu_open = 0;
                                need_redraw = 1;
                                break;
                            }
                        }
                        continue;
                    } else {
                        /* Click outside Start menu -> close it */
                        st.menu_open = 0;
                        need_redraw = 1;
                    }
                }

                /* C. Check Quick Launch Icons */
                int qx = 102;
                int q_clicked = 0;
                for (int q = 0; q < 4; ++q) {
                    if (px >= qx && px <= qx + 28 && py >= panel_y + 4 && py <= panel_y + 34) {
                        const char *q_execs[4] = {"/bin/terminal", "/bin/pcmanfm", "/bin/notepad", "/bin/webviewer"};
                        launch_app(q_execs[q]);
                        q_clicked = 1;
                        break;
                    }
                    qx += 34;
                }
                if (q_clicked) continue;

                /* D. Check Desktop Shortcut Icons (Double or Single Click) */
                for (size_t i = 0; i < SHORTCUT_COUNT; ++i) {
                    struct desktop_shortcut *sc = &SHORTCUTS[i];
                    if (px >= sc->x && px <= sc->x + sc->w &&
                        py >= sc->y && py <= sc->y + sc->h) {
                        launch_app(sc->exec);
                        need_redraw = 1;
                        break;
                    }
                }

                /* E. Check Power / Logout Button */
                int pwr_x = (int)surface_width - 32;
                if (px >= pwr_x && px <= pwr_x + 24 && py >= panel_y + 6 && py <= panel_y + 30) {
                    launch_app("/bin/poweroff");
                }
            }
        }

        /* 2. Clock Tick & Animation */
        loop_tick++;
        if (loop_tick >= 10) { /* ~1 second tick */
            loop_tick = 0;
            st.clock_seconds++;
            if (st.clock_seconds >= 60) {
                st.clock_seconds = 0;
                st.clock_minutes++;
                if (st.clock_minutes >= 60) {
                    st.clock_minutes = 0;
                    st.clock_hours = (st.clock_hours + 1) % 24;
                }
            }
            need_redraw = 1;
        }

        /* 3. Redraw and damage if needed */
        if (need_redraw) {
            render_full_desktop(surface.pixels, surface_width, surface_height,
                                channel, profile, &st);
            send_damage(channel, surface_id, surface_width, surface_height);
        }

        if (once) break;
        (void)poll(NULL, 0, 100);
    }

    send_destroy(channel, surface_id);
    destroy_surface(&surface);
    stop_session_components();
    unregister_component_service();
    (void)akaos_toolkit_disconnect(channel);
    return 0;
}
