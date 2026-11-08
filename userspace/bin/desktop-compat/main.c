#include <akaos/dbus.h>
#include <akaos/display.h>
#include <akaos/ui_font_aa.h>
#include <akaos/toolkit.h>
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

#define PANEL_HEIGHT 58U
#define SURFACE_NAME_SIZE 32U
#define PROFILE_COMPONENT_CAPACITY 2U
#if UINTPTR_MAX <= UINT32_MAX
#define COMPAT_SURFACE_WIDTH 320U
#define COMPAT_SURFACE_HEIGHT 240U
#else
#define COMPAT_SURFACE_WIDTH 640U
#define COMPAT_SURFACE_HEIGHT 400U
#endif

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
    PROFILE_GNOME,
    PROFILE_KDE,
    PROFILE_OPENBOX,
    /* Wayland Compositors */
    PROFILE_SWAY,
    PROFILE_RIVER,
    PROFILE_NIRI,
    PROFILE_WAYFIRE,
    PROFILE_LABWC,
    PROFILE_WESTON,
    /* X11 Tiling Window Managers */
    PROFILE_QTILE,
    PROFILE_AWESOME,
    PROFILE_XMONAD,
    PROFILE_HERBSTLUFTWM,
    PROFILE_SPECTRWM,
    /* Lightweight Window Managers */
    PROFILE_FLUXBOX,
    PROFILE_ICEWM,
    PROFILE_JWM,
    PROFILE_WMAKER,
    PROFILE_FVWM,
    /* Desktop Environments */
    PROFILE_BUDGIE,
    PROFILE_COSMIC,
    PROFILE_DEEPIN,
    PROFILE_PANTHEON,
    PROFILE_UKUI,
    PROFILE_ENLIGHTENMENT
};

#if defined(__clang__)
#define AKAOS_PROFILE_NOOPT __attribute__((noinline, optnone))
#else
#define AKAOS_PROFILE_NOOPT
#endif

/* Keep profile metadata in switch statements rather than pointer tables.
 * akaOS static images are linked at VMA zero and relocated as a whole by the
 * kernel; switch-returned string literals remain position-independent. */
static enum profile_kind profile_for_name(const char *name) {
    if (!name) return PROFILE_LXDE;
    if (strstr(name, "sway")) return PROFILE_SWAY;
    if (strstr(name, "river")) return PROFILE_RIVER;
    if (strstr(name, "niri")) return PROFILE_NIRI;
    if (strstr(name, "wayfire")) return PROFILE_WAYFIRE;
    if (strstr(name, "labwc")) return PROFILE_LABWC;
    if (strstr(name, "weston")) return PROFILE_WESTON;
    if (strstr(name, "qtile")) return PROFILE_QTILE;
    if (strstr(name, "awesome")) return PROFILE_AWESOME;
    if (strstr(name, "xmonad")) return PROFILE_XMONAD;
    if (strstr(name, "herbstluftwm")) return PROFILE_HERBSTLUFTWM;
    if (strstr(name, "spectrwm")) return PROFILE_SPECTRWM;
    if (strstr(name, "fluxbox")) return PROFILE_FLUXBOX;
    if (strstr(name, "icewm")) return PROFILE_ICEWM;
    if (strstr(name, "jwm")) return PROFILE_JWM;
    if (strstr(name, "wmaker") || strstr(name, "WindowMaker")) return PROFILE_WMAKER;
    if (strstr(name, "fvwm")) return PROFILE_FVWM;
    if (strstr(name, "budgie")) return PROFILE_BUDGIE;
    if (strstr(name, "cosmic")) return PROFILE_COSMIC;
    if (strstr(name, "deepin") || strstr(name, "dde")) return PROFILE_DEEPIN;
    if (strstr(name, "pantheon")) return PROFILE_PANTHEON;
    if (strstr(name, "ukui")) return PROFILE_UKUI;
    if (strstr(name, "enlightenment") || strstr(name, "e26")) return PROFILE_ENLIGHTENMENT;
    if (strstr(name, "lxqt") || strstr(name, "pcmanfm-qt")) return PROFILE_LXQT;
    if (strstr(name, "gnome") || strstr(name, "mutter")) return PROFILE_GNOME;
    if (strstr(name, "plasma") || strstr(name, "kwin")) return PROFILE_KDE;
    if (strstr(name, "openbox") || strstr(name, "pcmanfm")) return PROFILE_OPENBOX;
    return PROFILE_LXDE;
}

static const char *AKAOS_PROFILE_NOOPT profile_id(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_SWAY: return "sway";
    case PROFILE_RIVER: return "river";
    case PROFILE_NIRI: return "niri";
    case PROFILE_WAYFIRE: return "wayfire";
    case PROFILE_LABWC: return "labwc";
    case PROFILE_WESTON: return "weston";
    case PROFILE_QTILE: return "qtile";
    case PROFILE_AWESOME: return "awesome";
    case PROFILE_XMONAD: return "xmonad";
    case PROFILE_HERBSTLUFTWM: return "herbstluftwm";
    case PROFILE_SPECTRWM: return "spectrwm";
    case PROFILE_FLUXBOX: return "fluxbox";
    case PROFILE_ICEWM: return "icewm";
    case PROFILE_JWM: return "jwm";
    case PROFILE_WMAKER: return "wmaker";
    case PROFILE_FVWM: return "fvwm";
    case PROFILE_BUDGIE: return "budgie";
    case PROFILE_COSMIC: return "cosmic";
    case PROFILE_DEEPIN: return "deepin";
    case PROFILE_PANTHEON: return "pantheon";
    case PROFILE_UKUI: return "ukui";
    case PROFILE_ENLIGHTENMENT: return "enlightenment";
    case PROFILE_LXQT: return "lxqt";
    case PROFILE_GNOME: return "gnome";
    case PROFILE_KDE: return "kde";
    case PROFILE_OPENBOX: return "openbox";
    default: return "lxde";
    }
}

static const char *AKAOS_PROFILE_NOOPT profile_label(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_SWAY: return "Sway";
    case PROFILE_RIVER: return "river";
    case PROFILE_NIRI: return "niri";
    case PROFILE_WAYFIRE: return "Wayfire";
    case PROFILE_LABWC: return "labwc";
    case PROFILE_WESTON: return "Weston";
    case PROFILE_QTILE: return "Qtile";
    case PROFILE_AWESOME: return "awesome";
    case PROFILE_XMONAD: return "xmonad";
    case PROFILE_HERBSTLUFTWM: return "herbstluftwm";
    case PROFILE_SPECTRWM: return "spectrwm";
    case PROFILE_FLUXBOX: return "Fluxbox";
    case PROFILE_ICEWM: return "IceWM";
    case PROFILE_JWM: return "JWM";
    case PROFILE_WMAKER: return "Window Maker";
    case PROFILE_FVWM: return "FVWM";
    case PROFILE_BUDGIE: return "Budgie";
    case PROFILE_COSMIC: return "COSMIC";
    case PROFILE_DEEPIN: return "Deepin";
    case PROFILE_PANTHEON: return "Pantheon";
    case PROFILE_UKUI: return "UKUI";
    case PROFILE_ENLIGHTENMENT: return "Enlightenment";
    case PROFILE_LXQT: return "LXQt";
    case PROFILE_GNOME: return "GNOME";
    case PROFILE_KDE: return "KDE Plasma";
    case PROFILE_OPENBOX: return "Openbox";
    default: return "LXDE";
    }
}

static const char *AKAOS_PROFILE_NOOPT profile_subtitle(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXQT: return "Lightweight Qt-compatible session";
    case PROFILE_GNOME: return "GTK and GIO-compatible session";
    case PROFILE_KDE: return "Qt and KWin-compatible session";
    case PROFILE_OPENBOX: return "Window-manager compatibility session";
    default: return "Lightweight X11-compatible session";
    }
}

static const char *AKAOS_PROFILE_NOOPT profile_icon(enum profile_kind profile) {
    return profile_id(profile);
}

/* A desktop session is more than a single shell surface.  Keep the component
 * graph explicit and bounded so the native profile can start the same
 * window-manager/panel/session roles that upstream desktops expect while the
 * individual compatibility binaries continue to share this small renderer. */
static size_t profile_component_count(enum profile_kind profile) {
    switch (profile) {
    case PROFILE_LXDE: return 2U;       /* Openbox + PCManFM */
    case PROFILE_LXQT: return 2U;       /* LXQt panel + PCManFM-Qt */
    case PROFILE_GNOME: return 2U;      /* Mutter + GNOME session */
    case PROFILE_KDE: return 2U;        /* KWin + Plasma session */
    default: return 0U;
    }
}

static const char *AKAOS_PROFILE_NOOPT profile_component(
    enum profile_kind profile, size_t index) {
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

static uint32_t profile_color(enum profile_kind profile, unsigned slot) {
    switch (profile) {
    case PROFILE_LXQT:
        return slot == 0 ? UINT32_C(0x171f3b) :
               slot == 1 ? UINT32_C(0x252d50) :
               slot == 2 ? UINT32_C(0x9c8cff) : UINT32_C(0xf6f5ff);
    case PROFILE_GNOME:
        return slot == 0 ? UINT32_C(0x241b35) :
               slot == 1 ? UINT32_C(0x34264d) :
               slot == 2 ? UINT32_C(0xf28acb) : UINT32_C(0xfff5fc);
    case PROFILE_KDE:
        return slot == 0 ? UINT32_C(0x102d45) :
               slot == 1 ? UINT32_C(0x174666) :
               slot == 2 ? UINT32_C(0x4ed2e8) : UINT32_C(0xf1fcff);
    case PROFILE_OPENBOX:
        return slot == 0 ? UINT32_C(0x1f2630) :
               slot == 1 ? UINT32_C(0x313b4a) :
               slot == 2 ? UINT32_C(0x88b7e8) : UINT32_C(0xf6f9ff);
    default:
        return slot == 0 ? UINT32_C(0x10243a) :
               slot == 1 ? UINT32_C(0x173b5f) :
               slot == 2 ? UINT32_C(0x5bb8ff) : UINT32_C(0xf2f7ff);
    }
}

static const char *program_name(const char *path) {
    if (!path) return "desktop";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Components are packaged as aliases of this compatibility binary.  Keep
 * their role identity explicit so the session bus can supervise each role
 * independently, just as a real desktop registers separate activation
 * providers for its window manager, panel, and session services. */
enum component_role {
    COMPONENT_NONE = 0,
    COMPONENT_OPENBOX,
    COMPONENT_PCFMAN,
    COMPONENT_LXQT_PANEL,
    COMPONENT_PCFMAN_QT,
    COMPONENT_MUTTER,
    COMPONENT_GNOME_SESSION,
    COMPONENT_KWIN,
    COMPONENT_PLASMA
};

static enum component_role component_role_for_name(const char *name) {
    if (!name) return COMPONENT_NONE;
    if (strstr(name, "lxqt-panel")) return COMPONENT_LXQT_PANEL;
    if (strstr(name, "pcmanfm-qt")) return COMPONENT_PCFMAN_QT;
    if (strstr(name, "pcmanfm")) return COMPONENT_PCFMAN;
    if (strstr(name, "openbox")) return COMPONENT_OPENBOX;
    if (strstr(name, "gnome-session")) return COMPONENT_GNOME_SESSION;
    if (strstr(name, "mutter")) return COMPONENT_MUTTER;
    if (strstr(name, "startplasma")) return COMPONENT_PLASMA;
    if (strstr(name, "kwin")) return COMPONENT_KWIN;
    return COMPONENT_NONE;
}

static const char *component_service_name(enum component_role role) {
    switch (role) {
    case COMPONENT_OPENBOX: return "org.akaos.LXDE.Openbox";
    case COMPONENT_PCFMAN: return "org.akaos.LXDE.PCManFM";
    case COMPONENT_LXQT_PANEL: return "org.akaos.LXQt.Panel";
    case COMPONENT_PCFMAN_QT: return "org.akaos.LXQt.FileManager";
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
    case COMPONENT_PCFMAN: return "/bin/pcmanfm";
    case COMPONENT_LXQT_PANEL: return "/bin/lxqt-panel";
    case COMPONENT_PCFMAN_QT: return "/bin/pcmanfm-qt";
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
        int result = akaos_dbus_release_name(component_name_descriptor,
                                              service_name);
        if (result == 0)
            printf("desktop component D-Bus well-known name released (%s)\n",
                   service_name);
        component_name_descriptor = -1;
    }
    if (!component_service_registered) {
        registered_service_name[0] = '\0';
        return;
    }
    int result = akaos_dbus_unregister_service(service_name);
    if (result == 0)
        printf("desktop component D-Bus activation service unregistered (%s)\n",
               service_name);
    registered_service_name[0] = '\0';
    component_service_registered = 0;
}

static int register_component_service(const char *name) {
    enum component_role role = component_role_for_name(name);
    const char *service = component_service_name(role);
    const char *executable = component_executable(role);
    if (!service || !executable) return 0;
    int metadata_result = akaos_dbus_register_service(
        service, executable, "--component", 0, 1000U);
    int metadata_owned = metadata_result == 0;
    if (metadata_result < 0 && errno != EEXIST) {
        printf("%s: unable to register D-Bus activation service (errno=%d)\n",
               name ? name : "desktop", errno);
        return -1;
    }
    if (metadata_owned) {
        strncpy(registered_service_name, service,
                sizeof(registered_service_name) - 1U);
        registered_service_name[sizeof(registered_service_name) - 1U] = '\0';
        component_service_registered = 1;
        printf("%s D-Bus activation service registered (%s)\n",
               name ? name : "desktop", service);
    } else {
        printf("%s D-Bus activation service already registered (%s)\n",
               name ? name : "desktop", service);
    }
    int descriptor = akaos_dbus_claim_name(service);
    if (descriptor < 0) {
        if (metadata_owned) {
            (void)akaos_dbus_unregister_service(service);
            component_service_registered = 0;
            registered_service_name[0] = '\0';
        }
        if (errno == EBUSY || errno == EEXIST) {
            printf("%s D-Bus well-known name already owned (%s)\n",
                   name ? name : "desktop", service);
            return 0;
        }
        printf("%s: unable to claim D-Bus well-known name (errno=%d)\n",
               name ? name : "desktop", errno);
        return -1;
    }
    strncpy(registered_service_name, service,
            sizeof(registered_service_name) - 1U);
    registered_service_name[sizeof(registered_service_name) - 1U] = '\0';
    component_name_descriptor = descriptor;
    printf("%s D-Bus well-known name claimed (%s)\n",
           name ? name : "desktop", service);
    /* Publish the first standard PropertiesChanged transition as soon as a
     * resident desktop role owns its bus name.  Consumers that subscribe
     * before activation can now discover readiness without polling. */
    if (akaos_dbus_emit_desktop_properties_changed(
            descriptor, "ready") != 0)
        printf("%s: unable to emit desktop readiness signal (errno=%d)\n",
               name ? name : "desktop", errno);
    return 0;
}

static uint32_t blend_channel(uint32_t left, uint32_t right, uint32_t step,
                              uint32_t total) {
    if (total == 0) return left;
    uint32_t inverse = total - step;
    return (left * inverse + right * step) / total;
}

static uint32_t blend_rgb(uint32_t left, uint32_t right, uint32_t step,
                          uint32_t total) {
    uint32_t r = blend_channel((left >> 16) & 0xffU, (right >> 16) & 0xffU,
                               step, total);
    uint32_t g = blend_channel((left >> 8) & 0xffU, (right >> 8) & 0xffU,
                               step, total);
    uint32_t b = blend_channel(left & 0xffU, right & 0xffU, step, total);
    return (r << 16) | (g << 8) | b;
}

static uint32_t pixel_value(uint32_t rgb) {
    /* Surface buffers are XRGB8888; Lumen converts the physical framebuffer
     * format while compositing, so keep the client ABI channel order stable. */
    return rgb;
}

static void fill_rect(uint32_t *pixels, uint32_t width, uint32_t height,
                      int x, int y, int rect_width, int rect_height,
                      uint32_t color) {
    if (!pixels || rect_width <= 0 || rect_height <= 0) return;
    for (int row = y; row < y + rect_height; ++row) {
        if (row < 0 || row >= (int)height) continue;
        for (int column = x; column < x + rect_width; ++column) {
            if (column < 0 || column >= (int)width) continue;
            pixels[(size_t)row * width + (size_t)column] = pixel_value(color);
        }
    }
}

static void draw_text(uint32_t *pixels, uint32_t width, uint32_t height,
                      int x, int y, const char *text, uint32_t color) {
    if (!pixels || !text) return;
    int origin = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        unsigned char character = (unsigned char)text[index];
        for (uint32_t row = 0; row < AKAOS_UI_FONT_HEIGHT; ++row) {
            for (uint32_t column = 0; column < AKAOS_UI_FONT_WIDTH; ++column) {
                uint8_t alpha = akaos_ui_font_get_alpha(character, row, column);
                if (!alpha) continue;
                int px = origin + (int)column;
                int py = y + (int)row;
                if (px < 0 || py < 0 || px >= (int)width || py >= (int)height)
                    continue;
                uint32_t background = pixels[(size_t)py * width + (size_t)px];
                uint32_t inverse = 255U - alpha;
                uint32_t r = (((background >> 16) & 0xffU) * inverse +
                              ((color >> 16) & 0xffU) * alpha + 127U) / 255U;
                uint32_t g = (((background >> 8) & 0xffU) * inverse +
                              ((color >> 8) & 0xffU) * alpha + 127U) / 255U;
                uint32_t b = ((background & 0xffU) * inverse +
                              (color & 0xffU) * alpha + 127U) / 255U;
                pixels[(size_t)py * width + (size_t)px] =
                    pixel_value((r << 16) | (g << 8) | b);
            }
        }
        origin += (int)akaos_ui_font_get_advance(character);
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
    message.z_order = 2;
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
        /* Let the first companion fault in its image and acquire the shared
         * compositor/D-Bus endpoints before launching the next role.  This
         * keeps startup deterministic on the small bounded scheduler while
         * still leaving both roles resident and independently supervised. */
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
        bus_ok = akaos_dbus_ping(bus) == 0;
        (void)akaos_dbus_close(bus);
    }
    if (!display_ok || !bus_ok) {
        printf("%s compatibility runtime failed (display=%d dbus=%d)\n",
               profile_label(profile), display_ok, bus_ok);
        return 1;
    }
    printf("%s compatibility runtime verified (%ux%u)\n", profile_label(profile),
           info.width, info.height);
    return 0;
}

static void run_component_service_loop(const char *process_name) {
    while (!session_stop_requested) {
        if (component_name_descriptor < 0) break;
        int dispatched = akaos_dbus_service_dispatch(
            component_name_descriptor, process_name ? process_name : "desktop",
            1000);
        if (dispatched < 0 && !session_stop_requested)
            session_stop_requested = 1;
    }
}

int main(int argc, char **argv) {
    const char *process_name = program_name(argc > 0 ? argv[0] : NULL);
    enum profile_kind profile = profile_for_name(process_name);
    int once = 0;
    int check = 0;
    int session_mode = 0;
    int component_mode = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0 ||
            strcmp(argv[index], "-h") == 0) {
            puts("Usage: desktop-profile [--check] [--once]\n"
                 "Native akaOS desktop compatibility session.");
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

    /* A resident session role owns a D-Bus name independently of the display
     * server.  Register it before connecting to Lumen so activation, status,
     * and introspection remain available during compositor restart or headless
     * startup recovery. */
    if (component_mode && !once &&
        register_component_service(process_name) != 0)
        return 3;

    struct akaos_compositor_channel *channel = NULL;
    if (akaos_toolkit_connect(&channel) != 0) {
        printf("%s: compositor is unavailable (errno=%d)\n",
               profile_label(profile), errno);
        if (component_mode && !once && component_name_descriptor >= 0) {
            printf("%s: D-Bus service remains active during compositor recovery\n",
                   profile_label(profile));
            run_component_service_loop(process_name);
            unregister_component_service();
            return 0;
        }
        unregister_component_service();
        return 1;
    }
    struct akaos_display_info display;
    if (akaos_toolkit_display_info(&display) != 0 || display.width == 0 ||
        display.height <= PANEL_HEIGHT) {
        printf("%s: display is unavailable\n", profile_label(profile));
        (void)akaos_toolkit_disconnect(channel);
        if (component_mode && !once && component_name_descriptor >= 0) {
            printf("%s: D-Bus service remains active during display recovery\n",
                   profile_label(profile));
            run_component_service_loop(process_name);
        }
        unregister_component_service();
        return 2;
    }

    ensure_display_protocol_bridge();

    int bus = akaos_dbus_connect(0);
    if (bus >= 0) {
        (void)akaos_dbus_ping(bus);
        (void)akaos_dbus_close(bus);
    }

    if (session_mode && !component_mode) {
        session_component_count = start_session_components(
            profile, once, session_component_pids,
            PROFILE_COMPONENT_CAPACITY);
        size_t started = session_component_count;
        printf("%s session components started (%d/%d)\n", profile_label(profile),
               (int)started, (int)profile_component_count(profile));
    }

    char name[SURFACE_NAME_SIZE];
    snprintf(name, sizeof(name), "/%s-session-%d", profile_id(profile), (int)getpid());
    uint32_t surface_width = display.width < COMPAT_SURFACE_WIDTH
                                 ? display.width : COMPAT_SURFACE_WIDTH;
    uint32_t surface_height = display.height > PANEL_HEIGHT &&
                                      display.height - PANEL_HEIGHT < COMPAT_SURFACE_HEIGHT
                                  ? display.height - PANEL_HEIGHT : COMPAT_SURFACE_HEIGHT;
    struct compat_surface surface =
        create_surface(name, surface_width, surface_height);
    if (!surface.header) {
        printf("%s: unable to create shared surface (errno=%d)\n",
               profile_label(profile), errno);
        unregister_component_service();
        stop_session_components();
        (void)akaos_toolkit_disconnect(channel);
        return 4;
    }

    for (uint32_t row = 0; row < surface_height; ++row) {
        uint32_t color = blend_rgb(profile_color(profile, 0), profile_color(profile, 1), row,
                                   surface_height ? surface_height - 1U : 1U);
        fill_rect(surface.pixels, surface_width, surface_height, 0, (int)row,
                  (int)surface_width, 1, color);
    }
    fill_rect(surface.pixels, surface_width, surface_height, 0,
              (int)surface_height - (int)PANEL_HEIGHT, (int)surface_width,
              (int)PANEL_HEIGHT, profile_color(profile, 1));
    fill_rect(surface.pixels, surface_width, surface_height, 0,
              (int)surface_height - (int)PANEL_HEIGHT, (int)surface_width, 2,
              profile_color(profile, 2));
    draw_text(surface.pixels, surface_width, surface_height, 34, 38,
              profile_label(profile), profile_color(profile, 3));
    draw_text(surface.pixels, surface_width, surface_height, 34, 64,
              profile_subtitle(profile), profile_color(profile, 3));
    draw_text(surface.pixels, surface_width, surface_height, 24,
              (int)surface_height - 38, profile_label(profile), profile_color(profile, 3));
    draw_text(surface.pixels, surface_width, surface_height,
              (int)surface_width - 210, (int)surface_height - 38,
              "akaOS session", profile_color(profile, 3));

    uint32_t surface_id = (uint32_t)getpid() * 17U + 9U;
    int32_t surface_x = (int32_t)(display.width - surface_width) / 2;
    int32_t surface_y = (int32_t)(display.height - surface_height - PANEL_HEIGHT) / 2;
    if (!register_desktop_surface(channel, surface_id, surface_width,
                                  surface_height, surface_x, surface_y, name,
                                  profile) ||
        !send_damage(channel, surface_id, surface_width, surface_height)) {
        printf("%s: compositor rejected desktop surface (errno=%d)\n",
               profile_label(profile), errno);
        destroy_surface(&surface);
        if (component_mode && !once && component_name_descriptor >= 0) {
            (void)akaos_toolkit_disconnect(channel);
            printf("%s: D-Bus service remains active during compositor recovery\n",
                   profile_label(profile));
            run_component_service_loop(process_name);
            unregister_component_service();
            return 0;
        }
        unregister_component_service();
        stop_session_components();
        (void)akaos_toolkit_disconnect(channel);
        return 5;
    }
    printf("%s session running\n", profile_label(profile));
    if (!once) {
        if (component_name_descriptor >= 0)
            run_component_service_loop(process_name);
        else
            while (!session_stop_requested) (void)poll(NULL, 0, 1000);
        stop_session_components();
    }
    send_destroy(channel, surface_id);
    destroy_surface(&surface);
    unregister_component_service();
    (void)akaos_toolkit_disconnect(channel);
    if (once) {
        /* A one-shot profile is a lifecycle test as well as a rendering test:
         * do not return until every companion has acknowledged its exit. */
        for (size_t index = 0; index < session_component_count; ++index) {
            if (session_component_pids[index] <= 0) continue;
            int status = 0;
            if (waitpid(session_component_pids[index], &status, 0) !=
                    session_component_pids[index] || !WIFEXITED(status) ||
                WEXITSTATUS(status) != 0) {
                session_component_count = 0U;
                return 6;
            }
            session_component_pids[index] = -1;
        }
        session_component_count = 0U;
        printf("%s session components stopped (%d/%d)\n",
               profile_label(profile), (int)profile_component_count(profile),
               (int)profile_component_count(profile));
    }
    return 0;
}
