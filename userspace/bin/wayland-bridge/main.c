#include <akaos/display.h>
#include <akaos/input.h>
#include <akaos/toolkit.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-server-protocol.h"
#include "wlr-layer-shell-server-protocol.h"
#include "wlr-foreign-toplevel-management-server-protocol.h"

#define wl_client_post_no_memory(c) do { \
    fprintf(stderr, "wayland-bridge: no-memory posted at %s:%d\n", __func__, __LINE__); \
    (wl_client_post_no_memory)(c); \
} while(0)

extern char **environ;

#define WAYLAND_SURFACE_LIMIT AKAOS_COMPOSITOR_MAX_SURFACES
#define WAYLAND_SEAT_LIMIT 16U
#define WAYLAND_MAX_DIMENSION 4096U
#define WAYLAND_TITLEBAR_HEIGHT 28U
#define WAYLAND_PANEL_HEIGHT 48U

struct bridge;
struct bridge_surface;
struct bridge_output;
struct bridge_foreign_manager;
struct bridge_foreign_handle;

struct bridge_seat {
    struct bridge *bridge;
    struct wl_resource *resource;
    struct wl_resource *pointer;
    struct wl_resource *keyboard;
    struct wl_resource *focused_surface;
    struct wl_resource *keyboard_focus_surface;
    struct bridge_surface *cursor_surface;
    struct wl_client *client;
    uint32_t last_pointer_press_serial;
    uint32_t last_pointer_enter_serial;
    uint32_t pressed_button;
    int32_t last_pointer_press_x;
    int32_t last_pointer_press_y;
    int32_t cursor_hotspot_x;
    int32_t cursor_hotspot_y;
};

struct bridge_positioner {
    int32_t width;
    int32_t height;
    int32_t anchor_x;
    int32_t anchor_y;
    int32_t anchor_width;
    int32_t anchor_height;
    int32_t offset_x;
    int32_t offset_y;
    uint32_t anchor;
    uint32_t gravity;
    uint32_t constraint_adjustment;
    int has_size;
    int has_anchor_rect;
};

struct bridge_layer_state {
    uint32_t layer;
    uint32_t width;
    uint32_t height;
    uint32_t anchor;
    uint32_t keyboard_interactivity;
    int32_t exclusive_zone;
    int32_t margin_top;
    int32_t margin_right;
    int32_t margin_bottom;
    int32_t margin_left;
};

struct bridge_surface {
    struct bridge *bridge;
    struct wl_resource *wl_surface;
    struct wl_resource *xdg_surface;
    struct wl_resource *toplevel;
    struct wl_resource *popup;
    struct wl_resource *layer_surface;
    struct wl_resource *subsurface_resource;
    struct bridge_surface *popup_parent;
    struct bridge_surface *subsurface_parent;
    struct bridge_foreign_handle *foreign_handles;
    struct wl_resource *pending_stack_sibling;
    struct wl_resource *pending_buffer;
    struct wl_resource *frame_callback;
    struct akaos_toolkit_surface compositor_surface;
    uint32_t surface_id;
    uint32_t configure_serial;
    uint32_t acknowledged_configure_serial;
    uint32_t resize_configure_serial;
    uint32_t resize_configure_width;
    uint32_t resize_configure_height;
    uint32_t resize_configure_edges;
    int32_t resize_configure_x;
    int32_t resize_configure_y;
    int32_t resize_anchor_right;
    int32_t resize_anchor_bottom;
    int32_t min_width;
    int32_t min_height;
    int32_t max_width;
    int32_t max_height;
    uint32_t window_state_flags;
    uint32_t pending_window_state_flags;
    int pending_window_state_valid;
    int minimized;
    int restore_geometry_valid;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    uint32_t buffer_generation;
    uint32_t z_order;
    uint32_t subsurface_order;
    uint32_t next_subsurface_order;
    uint32_t width;
    uint32_t height;
    struct bridge_layer_state initial_layer_state;
    struct bridge_layer_state layer_state;
    struct bridge_layer_state pending_layer_state;
    int32_t window_geometry_x;
    int32_t window_geometry_y;
    uint32_t window_geometry_width;
    uint32_t window_geometry_height;
    int32_t popup_x;
    int32_t popup_y;
    uint32_t popup_width;
    uint32_t popup_height;
    int32_t x;
    int32_t y;
    int configured;
    int resize_configure_pending;
    int attached;
    int has_pending_buffer;
    int has_current_buffer;
    int is_cursor;
    int is_layer_surface;
    int layer_state_pending;
    int has_committed;
    int is_subsurface;
    int is_subsurface_root;
    int subsurface_above_parent;
    int subsurface_synchronized;
    int commit_pending;
    int32_t subsurface_x;
    int32_t subsurface_y;
    int32_t pending_subsurface_x;
    int32_t pending_subsurface_y;
    int has_pending_subsurface_position;
    int has_pending_stack;
    int pending_stack_above;
    char title[256];
    char app_id[128];
    char shm_name[32];
    size_t mapping_size;
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

struct bridge_foreign_manager {
    struct bridge *bridge;
    struct wl_resource *resource;
    struct bridge_foreign_handle *handles;
    struct bridge_foreign_manager *next;
    int stopped;
};

struct bridge_foreign_handle {
    struct bridge_foreign_manager *manager;
    struct bridge_surface *surface;
    struct wl_resource *resource;
    struct bridge_foreign_handle *next_manager;
    struct bridge_foreign_handle *next_surface;
};

struct bridge_output {
    struct bridge *bridge;
    struct wl_resource *resource;
    struct bridge_output *next;
};

struct bridge {
    struct wl_display *display;
    struct akaos_compositor_channel *channel;
    struct akaos_display_info display_info;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    int keymap_fd;
    uint32_t keymap_size;
    struct bridge_surface *surfaces[WAYLAND_SURFACE_LIMIT];
    struct bridge_output *outputs;
    struct bridge_foreign_manager *foreign_managers;
    struct bridge_surface *grabbed_popup;
    struct bridge_seat seats[WAYLAND_SEAT_LIMIT];
    struct wl_event_source *input_timer;
    int escape_key_suppressed;
    struct bridge_surface *moving_surface;
    struct bridge_seat *moving_seat;
    int32_t move_offset_x;
    int32_t move_offset_y;
    struct bridge_surface *resizing_surface;
    struct bridge_seat *resizing_seat;
    uint32_t resize_edges;
    int32_t resize_start_pointer_x;
    int32_t resize_start_pointer_y;
    int32_t resize_start_x;
    int32_t resize_start_y;
    uint32_t resize_start_width;
    uint32_t resize_start_height;
    int32_t resize_desired_x;
    int32_t resize_desired_y;
    uint32_t resize_desired_width;
    uint32_t resize_desired_height;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t next_surface_id;
    uint32_t next_z_order;
};

static const struct xdg_surface_interface xdg_surface_implementation;
static const struct xdg_toplevel_interface xdg_toplevel_implementation;
static const struct xdg_popup_interface xdg_popup_implementation;
static const struct wl_subcompositor_interface subcompositor_implementation;
static const struct wl_subsurface_interface subsurface_implementation;
static const struct zwlr_layer_shell_v1_interface layer_shell_implementation;
static const struct zwlr_layer_surface_v1_interface layer_surface_implementation;
static const struct zwlr_foreign_toplevel_handle_v1_interface
    foreign_handle_implementation;
static void dismiss_grabbed_popup(struct bridge *bridge);
static void dismiss_grabbed_popup_to(struct bridge *bridge,
                                     struct bridge_surface *retained_popup);
static void toplevel_apply_window_state(struct bridge_surface *surface,
                                        uint32_t new_state_flags);
static void foreign_toplevel_map(struct bridge_surface *surface);
static void foreign_toplevel_unmap(struct bridge_surface *surface);
static void foreign_toplevel_send_metadata(struct bridge_surface *surface);
static void foreign_toplevel_send_state(struct bridge_surface *surface);
static void layer_surface_update_position(struct bridge_surface *surface,
                                          uint32_t width,
                                          uint32_t height);

static void copy_text(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0U) return;
    if (!source) source = "";
    size_t length = 0U;
    while (length + 1U < capacity && source[length]) ++length;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static uint32_t allocate_surface_id(struct bridge *bridge) {
    if (!bridge) return 0U;
    uint32_t surface_id = ++bridge->next_surface_id;
    if (surface_id == 0U) surface_id = ++bridge->next_surface_id;
    return surface_id;
}

static uint32_t flip_positioner_anchor(uint32_t value, int horizontal) {
    if (horizontal) {
        switch (value) {
            case XDG_POSITIONER_ANCHOR_LEFT:
                return XDG_POSITIONER_ANCHOR_RIGHT;
            case XDG_POSITIONER_ANCHOR_RIGHT:
                return XDG_POSITIONER_ANCHOR_LEFT;
            case XDG_POSITIONER_ANCHOR_TOP_LEFT:
                return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
            case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
                return XDG_POSITIONER_ANCHOR_TOP_LEFT;
            case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
                return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
            case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
                return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
            default:
                return value;
        }
    }
    switch (value) {
        case XDG_POSITIONER_ANCHOR_TOP:
            return XDG_POSITIONER_ANCHOR_BOTTOM;
        case XDG_POSITIONER_ANCHOR_BOTTOM:
            return XDG_POSITIONER_ANCHOR_TOP;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT:
            return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
            return XDG_POSITIONER_ANCHOR_TOP_LEFT;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
            return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
            return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
        default:
            return value;
    }
}

static uint32_t flip_positioner_gravity(uint32_t value, int horizontal) {
    if (horizontal) {
        switch (value) {
            case XDG_POSITIONER_GRAVITY_LEFT:
                return XDG_POSITIONER_GRAVITY_RIGHT;
            case XDG_POSITIONER_GRAVITY_RIGHT:
                return XDG_POSITIONER_GRAVITY_LEFT;
            case XDG_POSITIONER_GRAVITY_TOP_LEFT:
                return XDG_POSITIONER_GRAVITY_TOP_RIGHT;
            case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
                return XDG_POSITIONER_GRAVITY_TOP_LEFT;
            case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
                return XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
            case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
                return XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
            default:
                return value;
        }
    }
    switch (value) {
        case XDG_POSITIONER_GRAVITY_TOP:
            return XDG_POSITIONER_GRAVITY_BOTTOM;
        case XDG_POSITIONER_GRAVITY_BOTTOM:
            return XDG_POSITIONER_GRAVITY_TOP;
        case XDG_POSITIONER_GRAVITY_TOP_LEFT:
            return XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
        case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
            return XDG_POSITIONER_GRAVITY_TOP_LEFT;
        case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
            return XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
        case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
            return XDG_POSITIONER_GRAVITY_TOP_RIGHT;
        default:
            return value;
    }
}

static void positioner_anchor_point(const struct bridge_positioner *positioner,
                                    uint32_t anchor, int32_t *x, int32_t *y) {
    int32_t left = positioner->anchor_x;
    int32_t right = left + positioner->anchor_width;
    int32_t top = positioner->anchor_y;
    int32_t bottom = top + positioner->anchor_height;
    if (anchor == XDG_POSITIONER_ANCHOR_LEFT ||
        anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
        anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT) {
        *x = left;
    } else if (anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
               anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
               anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
        *x = right;
    } else {
        *x = left + positioner->anchor_width / 2;
    }
    if (anchor == XDG_POSITIONER_ANCHOR_TOP ||
        anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
        anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT) {
        *y = top;
    } else if (anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
               anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
               anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
        *y = bottom;
    } else {
        *y = top + positioner->anchor_height / 2;
    }
}

static void positioner_surface_origin(const struct bridge_positioner *positioner,
                                      uint32_t anchor, uint32_t gravity,
                                      int flip_x, int flip_y,
                                      int32_t *x, int32_t *y) {
    uint32_t adjusted_anchor = anchor;
    uint32_t adjusted_gravity = gravity;
    if (flip_x) {
        adjusted_anchor = flip_positioner_anchor(adjusted_anchor, 1);
        adjusted_gravity = flip_positioner_gravity(adjusted_gravity, 1);
    }
    if (flip_y) {
        adjusted_anchor = flip_positioner_anchor(adjusted_anchor, 0);
        adjusted_gravity = flip_positioner_gravity(adjusted_gravity, 0);
    }
    int32_t anchor_x = 0;
    int32_t anchor_y = 0;
    positioner_anchor_point(positioner, adjusted_anchor, &anchor_x, &anchor_y);

    int64_t origin_x = (int64_t)anchor_x +
        (flip_x ? -(int64_t)positioner->offset_x : positioner->offset_x);
    if (adjusted_gravity == XDG_POSITIONER_GRAVITY_LEFT ||
        adjusted_gravity == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
        adjusted_gravity == XDG_POSITIONER_GRAVITY_BOTTOM_LEFT) {
        origin_x -= positioner->width;
    } else if (adjusted_gravity != XDG_POSITIONER_GRAVITY_RIGHT &&
               adjusted_gravity != XDG_POSITIONER_GRAVITY_TOP_RIGHT &&
               adjusted_gravity != XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
        origin_x -= positioner->width / 2;
    }

    int64_t origin_y = (int64_t)anchor_y +
        (flip_y ? -(int64_t)positioner->offset_y : positioner->offset_y);
    if (adjusted_gravity == XDG_POSITIONER_GRAVITY_TOP ||
        adjusted_gravity == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
        adjusted_gravity == XDG_POSITIONER_GRAVITY_TOP_RIGHT) {
        origin_y -= positioner->height;
    } else if (adjusted_gravity != XDG_POSITIONER_GRAVITY_BOTTOM &&
               adjusted_gravity != XDG_POSITIONER_GRAVITY_BOTTOM_LEFT &&
               adjusted_gravity != XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
        origin_y -= positioner->height / 2;
    }
    if (origin_x > INT32_MAX) origin_x = INT32_MAX;
    if (origin_x < INT32_MIN) origin_x = INT32_MIN;
    if (origin_y > INT32_MAX) origin_y = INT32_MAX;
    if (origin_y < INT32_MIN) origin_y = INT32_MIN;
    *x = (int32_t)origin_x;
    *y = (int32_t)origin_y;
}

static void position_popup(const struct bridge_positioner *positioner,
                           const struct bridge *bridge,
                           int32_t *x, int32_t *y) {
    positioner_surface_origin(positioner, positioner->anchor,
                              positioner->gravity, 0, 0, x, y);
    if ((positioner->constraint_adjustment &
         XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X) &&
        (*x < 0 || (int64_t)*x + positioner->width >
                      bridge->display_info.width)) {
        int32_t flipped_x = 0;
        int32_t flipped_y = 0;
        positioner_surface_origin(positioner, positioner->anchor,
                                  positioner->gravity, 1, 0,
                                  &flipped_x, &flipped_y);
        if (flipped_x >= 0 &&
            (int64_t)flipped_x + positioner->width <=
                bridge->display_info.width)
            *x = flipped_x;
    }
    if ((positioner->constraint_adjustment &
         XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y) &&
        (*y < 0 || (int64_t)*y + positioner->height >
                      bridge->display_info.height)) {
        int32_t flipped_x = 0;
        int32_t flipped_y = 0;
        positioner_surface_origin(positioner, positioner->anchor,
                                  positioner->gravity, 0, 1,
                                  &flipped_x, &flipped_y);
        if (flipped_y >= 0 &&
            (int64_t)flipped_y + positioner->height <=
                bridge->display_info.height)
            *y = flipped_y;
    }
    if (positioner->constraint_adjustment &
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X) {
        int32_t maximum = (int32_t)bridge->display_info.width -
                          positioner->width;
        if (maximum < 0) maximum = 0;
        if (*x < 0) *x = 0;
        if (*x > maximum) *x = maximum;
    }
    if (positioner->constraint_adjustment &
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y) {
        int32_t maximum = (int32_t)bridge->display_info.height -
                          positioner->height;
        if (maximum < 0) maximum = 0;
        if (*y < 0) *y = 0;
        if (*y > maximum) *y = maximum;
    }
}

static void report_xkb_failure(const struct bridge *bridge, const char *stage) {
    fprintf(stderr, "wayland-bridge: XKB %s failed (errno %d: %s)\n",
            stage, errno, strerror(errno));
    const char *debug = getenv("AKAOS_XKB_DEBUG");
    if (!bridge || !bridge->xkb_context || !debug || !debug[0] ||
        debug[0] == '0') return;
    unsigned int path_count = xkb_context_num_include_paths(
        bridge->xkb_context);
    for (unsigned int index = 0; index < path_count; ++index) {
        fprintf(stderr, "wayland-bridge: XKB include path[%u]=%s\n", index,
                xkb_context_include_path_get(bridge->xkb_context, index));
    }
}

static int connect_compositor(struct bridge *bridge) {
    int last_rc = 0;
    for (unsigned attempt = 0U; attempt < 200U; ++attempt) {
        last_rc = akaos_toolkit_connect(&bridge->channel);
        if (last_rc == 0) {
            bridge->display_info.abi_version = bridge->channel->abi_version;
            bridge->display_info.width = bridge->channel->display_width;
            bridge->display_info.height = bridge->channel->display_height;
            bridge->display_info.stride_pixels = bridge->channel->display_width;
            bridge->display_info.pixel_format = AKAOS_DISPLAY_PIXEL_RGBX8888;
            bridge->display_info.byte_size =
                (uint64_t)bridge->display_info.width *
                bridge->display_info.height * sizeof(uint32_t);
            return 1;
        }
        (void)poll(NULL, 0U, 25);
    }
    fprintf(stderr, "wayland-bridge: connect_compositor failed after 200 attempts (last_rc=%d, errno=%d)\n",
            last_rc, errno);
    return 0;
}

static int create_keyboard_keymap(struct bridge *bridge) {
    bridge->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!bridge->xkb_context) {
        report_xkb_failure(bridge, "context creation");
        return 0;
    }
    const char *debug = getenv("AKAOS_XKB_DEBUG");
    if (debug && debug[0] && debug[0] != '0')
        xkb_context_set_log_level(bridge->xkb_context, XKB_LOG_LEVEL_DEBUG);
    struct xkb_rule_names names = {
        .rules = "evdev",
        .model = "pc105",
        .layout = "us",
        .variant = "",
        .options = "",
    };
    bridge->xkb_keymap = xkb_keymap_new_from_names(
        bridge->xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!bridge->xkb_keymap) {
        report_xkb_failure(bridge, "evdev/pc105/us keymap compilation");
        return 0;
    }
    bridge->xkb_state = xkb_state_new(bridge->xkb_keymap);
    if (!bridge->xkb_state) {
        report_xkb_failure(bridge, "keyboard state creation");
        return 0;
    }
    char *keymap_text = xkb_keymap_get_as_string(
        bridge->xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_text) {
        report_xkb_failure(bridge, "keymap serialization");
        return 0;
    }
    size_t keymap_size = strlen(keymap_text) + 1U;
    if (keymap_size > UINT32_MAX) {
        free(keymap_text);
        return 0;
    }
    int keymap_fd = memfd_create("akaos-wayland-keymap", MFD_CLOEXEC);
    if (keymap_fd < 0) {
        free(keymap_text);
        report_xkb_failure(bridge, "keymap memfd creation");
        return 0;
    }
    if (ftruncate(keymap_fd, (off_t)keymap_size) != 0) {
        int saved_errno = errno;
        close(keymap_fd);
        free(keymap_text);
        errno = saved_errno;
        report_xkb_failure(bridge, "keymap memfd sizing");
        return 0;
    }
    void *keymap_mapping = mmap(NULL, keymap_size,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                keymap_fd, 0);
    if (keymap_mapping == MAP_FAILED) {
        int saved_errno = errno;
        close(keymap_fd);
        free(keymap_text);
        errno = saved_errno;
        report_xkb_failure(bridge, "keymap memfd mapping");
        return 0;
    }
    memcpy(keymap_mapping, keymap_text, keymap_size);
    int unmap_result = munmap(keymap_mapping, keymap_size);
    int unmap_errno = errno;
    free(keymap_text);
    if (unmap_result != 0) {
        close(keymap_fd);
        errno = unmap_errno;
        report_xkb_failure(bridge, "keymap memfd unmapping");
        return 0;
    }
    if (lseek(keymap_fd, 0, SEEK_SET) < 0) {
        int saved_errno = errno;
        close(keymap_fd);
        errno = saved_errno;
        report_xkb_failure(bridge, "keymap memfd rewind");
        return 0;
    }
    bridge->keymap_fd = keymap_fd;
    bridge->keymap_size = (uint32_t)keymap_size;
    return 1;
}

static void release_keyboard_keymap(struct bridge *bridge) {
    if (!bridge) return;
    if (bridge->keymap_fd >= 0) close(bridge->keymap_fd);
    if (bridge->xkb_state) xkb_state_unref(bridge->xkb_state);
    if (bridge->xkb_keymap) xkb_keymap_unref(bridge->xkb_keymap);
    if (bridge->xkb_context) xkb_context_unref(bridge->xkb_context);
    bridge->keymap_fd = -1;
    bridge->xkb_state = NULL;
    bridge->xkb_keymap = NULL;
    bridge->xkb_context = NULL;
}

static int create_surface_buffer(struct bridge_surface *surface,
                                 uint32_t width, uint32_t height) {
    if (!surface || width == 0U || height == 0U ||
        width > WAYLAND_MAX_DIMENSION || height > WAYLAND_MAX_DIMENSION ||
        (uint64_t)width * height > SIZE_MAX / sizeof(uint32_t)) return 0;

    uint64_t pixel_bytes = (uint64_t)width * height * sizeof(uint32_t);
    if (pixel_bytes > SIZE_MAX - sizeof(struct akaos_surface_header)) return 0;
    size_t mapping_size = sizeof(struct akaos_surface_header) +
                          (size_t)pixel_bytes;
    ++surface->buffer_generation;
    if (surface->buffer_generation == 0U) ++surface->buffer_generation;
    char new_name[32];
    int name_length = snprintf(new_name, sizeof(new_name), "/wl-%u-%u-%u",
                               (uint32_t)getpid(), surface->surface_id,
                               surface->buffer_generation);
    if (name_length <= 0 || (size_t)name_length >= sizeof(new_name)) return 0;

    int descriptor = shm_open(new_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) return 0;
    if (ftruncate(descriptor, (off_t)mapping_size) != 0) {
        close(descriptor);
        shm_unlink(new_name);
        return 0;
    }
    void *mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, descriptor, 0);
    int saved_errno = errno;
    close(descriptor);
    errno = saved_errno;
    if (mapping == MAP_FAILED) {
        shm_unlink(new_name);
        return 0;
    }

    struct akaos_surface_header *header = mapping;
    memset(header, 0, sizeof(*header));
    header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    header->width = width;
    header->height = height;
    header->stride_pixels = width;
    header->format = AKAOS_SURFACE_FORMAT_ARGB8888;
    header->pixel_offset = sizeof(*header);
    header->byte_size = mapping_size;
    memset((uint8_t *)mapping + header->pixel_offset, 0, (size_t)pixel_bytes);

    if (surface->header) {
        (void)munmap(surface->header, surface->mapping_size);
        (void)shm_unlink(surface->shm_name);
    }
    surface->header = header;
    surface->pixels = (uint32_t *)((uint8_t *)mapping + header->pixel_offset);
    surface->mapping_size = mapping_size;
    surface->width = width;
    surface->height = height;
    copy_text(surface->shm_name, sizeof(surface->shm_name), new_name);
    return 1;
}

static uint32_t layer_surface_role_flags(
    const struct bridge_surface *surface) {
    if (!surface || !surface->is_layer_surface) return 0U;
    return AKAOS_SURFACE_FLAG_LAYER_SHELL |
           ((surface->layer_state.layer << AKAOS_SURFACE_LAYER_SHIFT) &
            AKAOS_SURFACE_LAYER_MASK);
}

static int publish_surface(struct bridge_surface *surface, int resized) {
    if (!surface || !surface->bridge || !surface->header) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = resized ? AKAOS_DISPLAY_MSG_RESIZE : AKAOS_DISPLAY_MSG_ATTACH;
    message.flags = AKAOS_SURFACE_FLAG_WAYLAND |
                    (surface->popup ? AKAOS_SURFACE_FLAG_POPUP : 0U) |
                    (surface->is_cursor ? AKAOS_SURFACE_FLAG_CURSOR : 0U) |
                    surface->window_state_flags |
                    layer_surface_role_flags(surface) |
                    (surface->minimized
                         ? AKAOS_SURFACE_FLAG_MINIMIZED : 0U) |
                    ((surface->is_subsurface || surface->is_subsurface_root)
                         ? AKAOS_SURFACE_FLAG_SUBSURFACE : 0U) |
                    (surface->is_subsurface &&
                     surface->subsurface_above_parent
                         ? AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE : 0U);
    message.surface_id = surface->surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = surface->x;
    message.y = surface->y;
    message.parent_surface_id = surface->is_subsurface &&
                                surface->subsurface_parent
        ? surface->subsurface_parent->surface_id : 0U;
    message.width = surface->width;
    message.height = surface->height;
    if (resized && surface->resize_configure_pending &&
        surface->acknowledged_configure_serial ==
            surface->resize_configure_serial)
        message.configure_serial = surface->resize_configure_serial;
    if (surface->is_subsurface) {
        message.z_order = surface->subsurface_order;
    } else {
        message.z_order = ++surface->bridge->next_z_order;
        if (message.z_order == 0U)
            message.z_order = ++surface->bridge->next_z_order;
    }
    surface->z_order = message.z_order;
    message.damage_w = surface->width;
    message.damage_h = surface->height;
    memcpy(message.shm_name, surface->shm_name,
           sizeof(message.shm_name));
    copy_text(message.title, sizeof(message.title),
              surface->title[0] ? surface->title : "Wayland Window");
    copy_text(message.icon_name, sizeof(message.icon_name), surface->app_id);
    if (!akaos_compositor_submit(surface->bridge->channel, &message)) {
        fprintf(stderr,
                "wayland-bridge: compositor_submit failed for %u (abi=%u/%u proto=%u/%u queue=%u/%u)\n",
                surface->surface_id,
                surface->bridge->channel ? surface->bridge->channel->abi_version : 0,
                AKAOS_DISPLAY_ABI_VERSION,
                surface->bridge->channel ? surface->bridge->channel->protocol_version : 0,
                AKAOS_COMPOSITOR_PROTOCOL_VERSION,
                surface->bridge->channel ? surface->bridge->channel->request_head : 0,
                surface->bridge->channel ? surface->bridge->channel->request_tail : 0);
        return 0;
    }
    if (surface->is_layer_surface && !resized) {
        size_t nonzero_pixels = 0U;
        size_t opaque_pixels = 0U;
        for (uint32_t y = 0; y < surface->height; ++y) {
            for (uint32_t x = 0; x < surface->width; ++x) {
                uint32_t pixel =
                    surface->pixels[(size_t)y * surface->header->stride_pixels + x];
                if (pixel != 0U) ++nonzero_pixels;
                if ((pixel >> 24) != 0U) ++opaque_pixels;
            }
        }
        fprintf(stderr,
                "wayland-bridge: layer-shell mapped id=%u pos=%d,%d size=%ux%u nonzero=%zu opaque=%zu\n",
                surface->surface_id, surface->x, surface->y,
                surface->width, surface->height,
                nonzero_pixels, opaque_pixels);
    }
    surface->compositor_surface.channel = surface->bridge->channel;
    surface->compositor_surface.surface_id = surface->surface_id;
    surface->compositor_surface.client_pid = (uint32_t)getpid();
    surface->attached = 1;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (!resized && surface->toplevel) foreign_toplevel_map(surface);
    return 1;
}

static int publish_layer_surface_role(struct bridge_surface *surface) {
    if (!surface || !surface->is_layer_surface || !surface->attached ||
        !surface->bridge || !surface->bridge->channel) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_STATE;
    message.flags = layer_surface_role_flags(surface);
    message.surface_id = surface->surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = surface->x;
    message.y = surface->y;
    return akaos_compositor_submit(surface->bridge->channel, &message);
}

static uint32_t constrain_toplevel_dimension(
    int64_t value, int32_t minimum, int32_t maximum) {
    uint32_t lower = minimum > 0 ? (uint32_t)minimum : 1U;
    uint32_t upper = maximum > 0 ? (uint32_t)maximum
                                 : WAYLAND_MAX_DIMENSION;
    if (upper > WAYLAND_MAX_DIMENSION) upper = WAYLAND_MAX_DIMENSION;
    if (lower > upper) lower = upper;
    if (value < (int64_t)lower) value = lower;
    if (value > (int64_t)upper) value = upper;
    return (uint32_t)value;
}

static int surface_has_keyboard_focus(const struct bridge_surface *surface) {
    if (!surface || !surface->bridge || !surface->wl_surface) return 0;
    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        if (surface->bridge->seats[index].keyboard_focus_surface ==
            surface->wl_surface)
            return 1;
    }
    return 0;
}

static int append_toplevel_configure_states(
    struct bridge_surface *surface, struct wl_array *states) {
    if (!surface || !surface->toplevel || !states) return 0;
    uint32_t values[4];
    size_t count = 0U;
    if (surface->window_state_flags & AKAOS_SURFACE_FLAG_MAXIMIZED)
        values[count++] = XDG_TOPLEVEL_STATE_MAXIMIZED;
    if (surface->window_state_flags & AKAOS_SURFACE_FLAG_FULLSCREEN)
        values[count++] = XDG_TOPLEVEL_STATE_FULLSCREEN;
    if (surface_has_keyboard_focus(surface))
        values[count++] = XDG_TOPLEVEL_STATE_ACTIVATED;
    if (surface->bridge && surface->bridge->resizing_surface == surface)
        values[count++] = XDG_TOPLEVEL_STATE_RESIZING;
    for (size_t index = 0; index < count; ++index) {
        uint32_t *entry = wl_array_add(states, sizeof(*entry));
        if (!entry) {
            wl_client_post_no_memory(wl_resource_get_client(surface->toplevel));
            return 0;
        }
        *entry = values[index];
    }
    return 1;
}

static int send_resize_configure(struct bridge_surface *surface,
                                 int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 int force) {
    if (!surface || !surface->bridge || !surface->toplevel ||
        !surface->xdg_surface || width == 0U || height == 0U ||
        width > WAYLAND_MAX_DIMENSION || height > WAYLAND_MAX_DIMENSION ||
        surface->resize_configure_pending)
        return 0;
    if (!force && surface->x == x && surface->y == y &&
        surface->width == width && surface->height == height)
        return 0;

    struct wl_array states;
    wl_array_init(&states);
    if (!append_toplevel_configure_states(surface, &states)) {
        wl_array_release(&states);
        return 0;
    }
    xdg_toplevel_send_configure(surface->toplevel, (int32_t)width,
                                (int32_t)height, &states);
    wl_array_release(&states);
    uint32_t serial = wl_display_next_serial(surface->bridge->display);
    xdg_surface_send_configure(surface->xdg_surface, serial);
    surface->configure_serial = serial;
    surface->resize_configure_serial = serial;
    surface->resize_configure_width = width;
    surface->resize_configure_height = height;
    surface->resize_configure_x = x;
    surface->resize_configure_y = y;
    surface->resize_configure_edges = surface->bridge->resize_edges;
    surface->resize_configure_pending = 1;
    return 1;
}

static void update_interactive_resize(struct bridge *bridge) {
    if (!bridge || !bridge->resizing_surface || !bridge->resizing_seat ||
        bridge->resizing_seat->pressed_button != AKAOS_BTN_LEFT)
        return;
    struct bridge_surface *surface = bridge->resizing_surface;
    uint32_t edges = bridge->resize_edges;
    int resize_left = (edges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) != 0U;
    int resize_right = (edges & XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) != 0U;
    int resize_top = (edges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) != 0U;
    int resize_bottom = (edges & XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) != 0U;
    int64_t dx = (int64_t)bridge->pointer_x - bridge->resize_start_pointer_x;
    int64_t dy = (int64_t)bridge->pointer_y - bridge->resize_start_pointer_y;
    int64_t width = bridge->resize_start_width;
    int64_t height = bridge->resize_start_height;
    if (resize_left) width -= dx;
    else if (resize_right) width += dx;
    if (resize_top) height -= dy;
    else if (resize_bottom) height += dy;

    bridge->resize_desired_width = constrain_toplevel_dimension(
        width, surface->min_width, surface->max_width);
    bridge->resize_desired_height = constrain_toplevel_dimension(
        height, surface->min_height, surface->max_height);
    bridge->resize_desired_x = resize_left
        ? bridge->resize_start_x + (int32_t)bridge->resize_start_width -
              (int32_t)bridge->resize_desired_width
        : bridge->resize_start_x;
    bridge->resize_desired_y = resize_top
        ? bridge->resize_start_y + (int32_t)bridge->resize_start_height -
              (int32_t)bridge->resize_desired_height
        : bridge->resize_start_y;

    if (!surface->resize_configure_pending)
        (void)send_resize_configure(surface, bridge->resize_desired_x,
                                    bridge->resize_desired_y,
                                    bridge->resize_desired_width,
                                    bridge->resize_desired_height, 0);
}

static int copy_shm_buffer(struct bridge_surface *surface,
                           struct wl_resource *buffer_resource) {
    struct wl_shm_buffer *buffer = wl_shm_buffer_get(buffer_resource);
    if (!surface || !buffer) {
        fprintf(stderr, "wayland-bridge: invalid wl_shm buffer on surface %u\n",
                surface ? surface->surface_id : 0U);
        return 0;
    }
    int32_t width = wl_shm_buffer_get_width(buffer);
    int32_t height = wl_shm_buffer_get_height(buffer);
    int32_t stride = wl_shm_buffer_get_stride(buffer);
    uint32_t format = wl_shm_buffer_get_format(buffer);
    if (width <= 0 || height <= 0 || stride < width * 4 ||
        (uint32_t)width > WAYLAND_MAX_DIMENSION ||
        (uint32_t)height > WAYLAND_MAX_DIMENSION ||
        (format != WL_SHM_FORMAT_XRGB8888 &&
         format != WL_SHM_FORMAT_ARGB8888)) {
        fprintf(stderr,
                "wayland-bridge: rejected SHM buffer surface=%u size=%dx%d stride=%d format=%u\n",
                surface->surface_id, width, height, stride, format);
        return 0;
    }

    int resized = surface->header &&
                  (surface->width != (uint32_t)width ||
                   surface->height != (uint32_t)height);
    int resize_configure_response = surface->resize_configure_pending &&
        surface->acknowledged_configure_serial ==
            surface->resize_configure_serial;
    int first_buffer = !surface->header;
    if (!surface->header || resized) {
        if (!create_surface_buffer(surface, (uint32_t)width, (uint32_t)height)) {
            fprintf(stderr,
                    "wayland-bridge: cannot allocate compositor buffer for surface=%u size=%dx%d errno=%d\n",
                    surface->surface_id, width, height, errno);
            return 0;
        }
    }
    if (surface->is_subsurface && surface->subsurface_parent) {
        surface->x = surface->subsurface_parent->x + surface->subsurface_x;
        surface->y = surface->subsurface_parent->y + surface->subsurface_y;
    } else if (surface->is_layer_surface) {
        layer_surface_update_position(surface, (uint32_t)width,
                                      (uint32_t)height);
    } else if (surface->toplevel && first_buffer &&
               !(surface->window_state_flags &
                 (AKAOS_SURFACE_FLAG_MAXIMIZED |
                  AKAOS_SURFACE_FLAG_FULLSCREEN))) {
        surface->x = surface->bridge->display_info.width > (uint32_t)width
            ? (int32_t)(surface->bridge->display_info.width - (uint32_t)width) / 2
            : 0;
        surface->y = surface->bridge->display_info.height > (uint32_t)height
            ? (int32_t)(surface->bridge->display_info.height - (uint32_t)height) / 2
            : 0;
    } else if (surface->is_subsurface_root && first_buffer &&
               !surface->toplevel && !surface->popup) {
        surface->x = surface->bridge->display_info.width > (uint32_t)width
            ? (int32_t)(surface->bridge->display_info.width - (uint32_t)width) / 2
            : 0;
        surface->y = surface->bridge->display_info.height > (uint32_t)height
            ? (int32_t)(surface->bridge->display_info.height - (uint32_t)height) / 2
            : 0;
    }
    if (resize_configure_response) {
        surface->x = (surface->resize_configure_edges &
                      XDG_TOPLEVEL_RESIZE_EDGE_LEFT)
            ? surface->resize_anchor_right - width
            : surface->resize_configure_x;
        surface->y = (surface->resize_configure_edges &
                      XDG_TOPLEVEL_RESIZE_EDGE_TOP)
            ? surface->resize_anchor_bottom - height
            : surface->resize_configure_y;
    }

    const char *debug = getenv("AKAOS_XKB_DEBUG");
    if (surface->toplevel && first_buffer && debug && debug[0] &&
        debug[0] != '0') {
        fprintf(stderr,
                "wayland-bridge: initial toplevel geometry pos=%d,%d size=%ux%u\n",
                surface->x, surface->y, surface->width, surface->height);
        fflush(stderr);
    }
    wl_shm_buffer_begin_access(buffer);
    const uint8_t *source = wl_shm_buffer_get_data(buffer);
    if (!source) {
        wl_shm_buffer_end_access(buffer);
        fprintf(stderr, "wayland-bridge: missing SHM data for surface=%u\n",
                surface->surface_id);
        return 0;
    }
    for (int32_t row = 0; row < height; ++row) {
        memcpy(surface->pixels + (size_t)row * (uint32_t)width,
               source + (size_t)row * (uint32_t)stride,
               (size_t)(uint32_t)width * sizeof(uint32_t));
        if (format == WL_SHM_FORMAT_XRGB8888) {
            uint32_t *pixels = surface->pixels + (size_t)row * (uint32_t)width;
            for (int32_t column = 0; column < width; ++column)
                pixels[column] |= UINT32_C(0xff000000);
        }
    }
    wl_shm_buffer_end_access(buffer);
    __atomic_thread_fence(__ATOMIC_RELEASE);

    if (!surface->attached) {
        int published = publish_surface(surface, 0);
        if (!published)
            fprintf(stderr, "wayland-bridge: surface attach failed for %u\n",
                    surface->surface_id);
        return published;
    }
    if (resized || resize_configure_response) {
        int published = publish_surface(surface, 1);
        if (!published)
            fprintf(stderr, "wayland-bridge: surface resize failed for %u\n",
                    surface->surface_id);
        if (published && resize_configure_response) {
            if (!akaos_toolkit_surface_ack_configure(
                    &surface->compositor_surface,
                    surface->resize_configure_serial)) {
                fprintf(stderr,
                        "wayland-bridge: configure ack queue full for %u\n",
                        surface->surface_id);
            } else {
                surface->resize_configure_pending = 0;
                if (surface->pending_window_state_valid) {
                    uint32_t pending_flags =
                        surface->pending_window_state_flags;
                    surface->pending_window_state_valid = 0;
                    toplevel_apply_window_state(surface, pending_flags);
                } else {
                    update_interactive_resize(surface->bridge);
                }
            }
        }
        return published;
    }
    int damaged = akaos_toolkit_surface_damage(&surface->compositor_surface,
                                                0U, 0U, (uint32_t)width,
                                                (uint32_t)height);
    if (!damaged)
        fprintf(stderr, "wayland-bridge: surface damage failed for %u\n",
                surface->surface_id);
    return damaged;
}

static void resource_destroy(struct wl_client *client,
                             struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void region_rectangle(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t x, int32_t y,
                             int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static const struct wl_region_interface region_implementation = {
    .destroy = resource_destroy,
    .add = region_rectangle,
    .subtract = region_rectangle,
};

static void unmap_surface(struct bridge_surface *surface) {
    if (!surface || !surface->attached) return;
    foreign_toplevel_unmap(surface);
    (void)akaos_toolkit_surface_destroy(&surface->compositor_surface);
    surface->attached = 0;
}

static void surface_destroyed(struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (surface->subsurface_resource) {
        wl_resource_set_user_data(surface->subsurface_resource, NULL);
        wl_resource_destroy(surface->subsurface_resource);
        surface->subsurface_resource = NULL;
    }
    if (surface->layer_surface) {
        struct wl_resource *layer_resource = surface->layer_surface;
        surface->layer_surface = NULL;
        wl_resource_destroy(layer_resource);
    }
    unmap_surface(surface);
    if (surface->frame_callback)
        wl_resource_destroy(surface->frame_callback);
    if (surface->header && surface->header != MAP_FAILED)
        (void)munmap(surface->header, surface->mapping_size);
    if (surface->shm_name[0]) (void)shm_unlink(surface->shm_name);
    if (surface->bridge) {
        if (surface->bridge->grabbed_popup == surface)
            surface->bridge->grabbed_popup = NULL;
        if (surface->bridge->moving_surface == surface) {
            surface->bridge->moving_surface = NULL;
            surface->bridge->moving_seat = NULL;
        }
        if (surface->bridge->resizing_surface == surface) {
            surface->bridge->resizing_surface = NULL;
            surface->bridge->resizing_seat = NULL;
        }
        for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
            struct bridge_surface *candidate =
                surface->bridge->surfaces[index];
            if (candidate && candidate != surface &&
                candidate->subsurface_parent == surface) {
                unmap_surface(candidate);
                candidate->subsurface_parent = NULL;
                candidate->is_subsurface = 0;
                candidate->commit_pending = 0;
            }
            if (candidate == surface)
                surface->bridge->surfaces[index] = NULL;
        }
        for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
            if (surface->bridge->seats[index].cursor_surface == surface)
                surface->bridge->seats[index].cursor_surface = NULL;
            if (surface->bridge->seats[index].focused_surface ==
                surface->wl_surface)
                surface->bridge->seats[index].focused_surface = NULL;
            if (surface->bridge->seats[index].keyboard_focus_surface ==
                surface->wl_surface)
                surface->bridge->seats[index].keyboard_focus_surface = NULL;
        }
    }
    free(surface);
}

static void surface_attach(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *buffer,
                           int32_t x, int32_t y) {
    (void)client;
    (void)x;
    (void)y;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        surface->pending_buffer = buffer;
        surface->has_pending_buffer = 1;
    }
}

static void surface_damage(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y,
                           int32_t width, int32_t height) {
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void surface_frame(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t callback_id) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct wl_resource *callback = wl_resource_create(
        client, &wl_callback_interface, 1, callback_id);
    if (!callback) {
        wl_client_post_no_memory(client);
        return;
    }
    if (surface && surface->frame_callback)
        wl_resource_destroy(surface->frame_callback);
    wl_resource_set_implementation(callback, NULL, NULL, NULL);
    if (surface) surface->frame_callback = callback;
}

static void surface_region(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *region) {
    (void)client;
    (void)resource;
    (void)region;
}

static void surface_transform(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t transform) {
    (void)client;
    (void)resource;
    (void)transform;
}

static void surface_scale(struct wl_client *client,
                          struct wl_resource *resource, int32_t scale) {
    (void)client;
    (void)resource;
    (void)scale;
}

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y) {
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
}

static void toplevel_state_geometry(
    const struct bridge_surface *surface, uint32_t state_flags,
    int32_t *x, int32_t *y, uint32_t *width, uint32_t *height) {
    if (!surface || !surface->bridge || !x || !y || !width || !height) return;
    const struct akaos_display_info *display = &surface->bridge->display_info;
    if (state_flags & AKAOS_SURFACE_FLAG_FULLSCREEN) {
        *x = 0;
        *y = 0;
        *width = display->width;
        *height = display->height;
        return;
    }
    if (state_flags & AKAOS_SURFACE_FLAG_MAXIMIZED) {
        uint32_t available_height = display->height >
            WAYLAND_TITLEBAR_HEIGHT + WAYLAND_PANEL_HEIGHT
                ? display->height - WAYLAND_TITLEBAR_HEIGHT - WAYLAND_PANEL_HEIGHT
                : display->height;
        *width = constrain_toplevel_dimension(display->width,
                                               surface->min_width,
                                               surface->max_width);
        *height = constrain_toplevel_dimension(available_height,
                                                surface->min_height,
                                                surface->max_height);
        *x = display->width > *width
            ? (int32_t)(display->width - *width) / 2 : 0;
        *y = WAYLAND_TITLEBAR_HEIGHT +
            (available_height > *height
                 ? (int32_t)(available_height - *height) / 2 : 0);
        return;
    }
    if (surface->restore_geometry_valid) {
        *x = surface->restore_x;
        *y = surface->restore_y;
        *width = surface->restore_width;
        *height = surface->restore_height;
        return;
    }
    *x = surface->x;
    *y = surface->y;
    *width = constrain_toplevel_dimension(
        surface->width ? surface->width : 640U,
        surface->min_width, surface->max_width);
    *height = constrain_toplevel_dimension(
        surface->height ? surface->height : 480U,
        surface->min_height, surface->max_height);
}

static void layer_surface_begin_state(struct bridge_surface *surface) {
    if (!surface || surface->layer_state_pending) return;
    surface->pending_layer_state = surface->layer_state;
    surface->layer_state_pending = 1;
}

static int layer_surface_apply_pending_state(
    struct bridge_surface *surface) {
    if (!surface || !surface->layer_state_pending) return 0;
    surface->layer_state = surface->pending_layer_state;
    surface->layer_state_pending = 0;
    return 1;
}

static uint32_t layer_surface_configure_dimension(
    uint32_t requested, uint32_t output_size, uint32_t start_anchor,
    uint32_t end_anchor, int32_t start_margin, int32_t end_margin) {
    if (requested != 0U ||
        !start_anchor || !end_anchor) return requested;
    int64_t available = (int64_t)output_size - start_margin - end_margin;
    if (available <= 0) return 0U;
    if (available > WAYLAND_MAX_DIMENSION)
        available = WAYLAND_MAX_DIMENSION;
    return (uint32_t)available;
}

static int32_t layer_surface_axis_position(
    uint32_t output_size, uint32_t surface_size, uint32_t start_anchor,
    uint32_t end_anchor, int32_t start_margin, int32_t end_margin) {
    int64_t position;
    if (start_anchor && end_anchor) {
        int64_t available = (int64_t)output_size - start_margin - end_margin;
        position = start_margin + (available - surface_size) / 2;
    } else if (start_anchor) {
        position = start_margin;
    } else if (end_anchor) {
        position = (int64_t)output_size - surface_size - end_margin;
    } else {
        position = ((int64_t)output_size - surface_size) / 2;
    }
    if (position < INT32_MIN) position = INT32_MIN;
    if (position > INT32_MAX) position = INT32_MAX;
    return (int32_t)position;
}

static void layer_surface_configure_geometry(
    const struct bridge_surface *surface, uint32_t *width,
    uint32_t *height) {
    if (!surface || !surface->bridge || !width || !height) return;
    const struct bridge_layer_state *state = &surface->layer_state;
    const struct akaos_display_info *display = &surface->bridge->display_info;
    *width = layer_surface_configure_dimension(
        state->width, display->width,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0U,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0U,
        state->margin_left, state->margin_right);
    *height = layer_surface_configure_dimension(
        state->height, display->height,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0U,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0U,
        state->margin_top, state->margin_bottom);
}

static void layer_surface_update_position(struct bridge_surface *surface,
                                          uint32_t width, uint32_t height) {
    if (!surface || !surface->bridge) return;
    const struct bridge_layer_state *state = &surface->layer_state;
    const struct akaos_display_info *display = &surface->bridge->display_info;
    surface->x = layer_surface_axis_position(
        display->width, width,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0U,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0U,
        state->margin_left, state->margin_right);
    surface->y = layer_surface_axis_position(
        display->height, height,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0U,
        (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0U,
        state->margin_top, state->margin_bottom);
}

static void layer_surface_send_configure(struct bridge_surface *surface) {
    if (!surface || !surface->layer_surface || !surface->bridge) return;
    uint32_t width = 0U;
    uint32_t height = 0U;
    layer_surface_configure_geometry(surface, &width, &height);
    surface->configure_serial =
        wl_display_next_serial(surface->bridge->display);
    zwlr_layer_surface_v1_send_configure(surface->layer_surface,
                                         surface->configure_serial,
                                         width, height);
    fprintf(stderr,
            "wayland-bridge: layer-shell configure id=%u layer=%u size=%ux%u\n",
            surface->surface_id, surface->layer_state.layer, width, height);
    surface->configured = 1;
}

static int layer_surface_state_is_valid(
    const struct bridge_surface *surface) {
    if (!surface) return 0;
    const struct bridge_layer_state *state = &surface->layer_state;
    uint32_t horizontal = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    uint32_t vertical = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (state->width == 0U && (state->anchor & horizontal) != horizontal)
        return 0;
    if (state->height == 0U && (state->anchor & vertical) != vertical)
        return 0;
    uint32_t width = 0U;
    uint32_t height = 0U;
    layer_surface_configure_geometry(surface, &width, &height);
    if ((state->width == 0U && width == 0U) ||
        (state->height == 0U && height == 0U)) return 0;
    return state->width <= WAYLAND_MAX_DIMENSION &&
           state->height <= WAYLAND_MAX_DIMENSION;
}

static void send_initial_configure(struct bridge_surface *surface) {
    if (!surface || surface->configured) return;
    if (surface->is_layer_surface) {
        layer_surface_send_configure(surface);
        return;
    }
    if (!surface->xdg_surface) return;
    if (surface->toplevel) {
        struct wl_array states;
        wl_array_init(&states);
        if (!append_toplevel_configure_states(surface, &states)) {
            wl_array_release(&states);
            return;
        }
        int32_t x = surface->x;
        int32_t y = surface->y;
        uint32_t width = 640U;
        uint32_t height = 480U;
        toplevel_state_geometry(surface, surface->window_state_flags,
                                &x, &y, &width, &height);
        surface->x = x;
        surface->y = y;
        xdg_toplevel_send_configure(surface->toplevel, (int32_t)width,
                                    (int32_t)height, &states);
        wl_array_release(&states);
    } else if (surface->popup) {
        xdg_popup_send_configure(surface->popup, surface->popup_x,
                                 surface->popup_y,
                                 (int32_t)surface->popup_width,
                                 (int32_t)surface->popup_height);
    } else {
        return;
    }
    surface->configure_serial = wl_display_next_serial(surface->bridge->display);
    xdg_surface_send_configure(surface->xdg_surface,
                               surface->configure_serial);
    surface->configured = 1;
}

static int surface_is_effectively_synchronized(
    const struct bridge_surface *surface) {
    for (const struct bridge_surface *current = surface;
         current && current->subsurface_parent;
         current = current->subsurface_parent) {
        if (current->subsurface_synchronized) return 1;
    }
    return 0;
}

static void update_subsurface_position(struct bridge_surface *surface) {
    if (!surface || !surface->is_subsurface || !surface->subsurface_parent)
        return;
    int32_t x = surface->subsurface_parent->x + surface->subsurface_x;
    int32_t y = surface->subsurface_parent->y + surface->subsurface_y;
    int changed = surface->x != x || surface->y != y;
    surface->x = x;
    surface->y = y;
    if (changed && surface->attached)
        (void)akaos_toolkit_surface_move(&surface->compositor_surface, x, y);
}

static int submit_surface_stack(struct bridge_surface *surface,
                                uint32_t z_order) {
    if (!surface || !surface->bridge || !surface->bridge->channel ||
        !surface->attached)
        return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_STACK;
    message.flags = AKAOS_SURFACE_FLAG_WAYLAND |
                    AKAOS_SURFACE_FLAG_SUBSURFACE |
                    (surface->subsurface_above_parent
                         ? AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE : 0U);
    message.surface_id = surface->surface_id;
    message.client_pid = (uint32_t)getpid();
    message.z_order = z_order;
    message.parent_surface_id = surface->subsurface_parent->surface_id;
    if (!akaos_compositor_submit(surface->bridge->channel, &message)) return 0;
    surface->z_order = z_order;
    return 1;
}

static int submit_subsurface_stack(struct bridge_surface *surface) {
    if (!surface || !surface->bridge || !surface->bridge->channel ||
        !surface->is_subsurface || !surface->subsurface_parent ||
        !surface->has_pending_stack || !surface->attached)
        return 0;
    struct bridge_surface *sibling = surface->pending_stack_sibling
        ? wl_resource_get_user_data(surface->pending_stack_sibling)
        : surface->subsurface_parent;
    if (!sibling) return 0;
    struct bridge_surface *parent = surface->subsurface_parent;
    int above_parent = sibling == parent
        ? surface->pending_stack_above
        : sibling->subsurface_above_parent;
    struct bridge_surface *siblings[WAYLAND_SURFACE_LIMIT];
    size_t sibling_count = 0U;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
        struct bridge_surface *candidate = parent->bridge->surfaces[index];
        if (!candidate || candidate == surface ||
            !candidate->is_subsurface ||
            candidate->subsurface_parent != parent ||
            candidate->subsurface_above_parent != above_parent)
            continue;
        size_t position = sibling_count;
        while (position > 0U &&
               siblings[position - 1U]->subsurface_order >
                   candidate->subsurface_order) {
            siblings[position] = siblings[position - 1U];
            --position;
        }
        siblings[position] = candidate;
        ++sibling_count;
    }

    size_t insert_at = sibling_count;
    if (sibling != parent) {
        size_t sibling_index = 0U;
        while (sibling_index < sibling_count &&
               siblings[sibling_index] != sibling)
            ++sibling_index;
        if (sibling_index == sibling_count) return 0;
        insert_at = sibling_index +
                    (surface->pending_stack_above ? 1U : 0U);
    }
    if (insert_at > sibling_count) insert_at = sibling_count;
    for (size_t index = insert_at; index < sibling_count; ++index) {
        siblings[index]->subsurface_order = (uint32_t)(index + 1U);
        (void)submit_surface_stack(siblings[index],
                                   siblings[index]->subsurface_order);
    }
    surface->subsurface_above_parent = above_parent;
    surface->subsurface_order = (uint32_t)insert_at;
    (void)submit_surface_stack(surface, surface->subsurface_order);
    surface->has_pending_stack = 0;
    surface->pending_stack_sibling = NULL;
    return 1;
}

static int apply_surface_commit(struct bridge_surface *surface) {
    if (!surface) return 0;

    int layer_state_changed = 0;
    if (surface->is_layer_surface) {
        if (!surface->configured) {
            if (surface->has_pending_buffer && surface->pending_buffer) {
                wl_resource_post_error(
                    surface->layer_surface,
                    ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                    "layer surface must commit without a buffer before mapping");
                return 0;
            }
            (void)layer_surface_apply_pending_state(surface);
            if (!layer_surface_state_is_valid(surface)) {
                wl_resource_post_error(
                    surface->layer_surface,
                    ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
                    "layer surface size requires valid opposing anchors");
                return 0;
            }
            send_initial_configure(surface);
            surface->commit_pending = 0;
            return surface->configured;
        }
        if (surface->has_pending_buffer && surface->pending_buffer &&
            surface->acknowledged_configure_serial !=
                surface->configure_serial) {
            wl_resource_post_error(
                surface->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                "layer surface buffer commit must acknowledge configure");
            return 0;
        }
        layer_state_changed =
            layer_surface_apply_pending_state(surface);
        if (layer_state_changed && !layer_surface_state_is_valid(surface)) {
            wl_resource_post_error(
                surface->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
                "layer surface size requires valid opposing anchors");
            return 0;
        }
    }

    send_initial_configure(surface);
    if (surface->has_pending_buffer) {
        struct wl_resource *buffer = surface->pending_buffer;
        surface->pending_buffer = NULL;
        surface->has_pending_buffer = 0;
        if (!buffer) {
            surface->has_current_buffer = 0;
            if (surface->attached) unmap_surface(surface);
            if (surface->is_layer_surface) {
                surface->configured = 0;
                surface->configure_serial = 0U;
                surface->acknowledged_configure_serial = 0U;
                surface->layer_state = surface->initial_layer_state;
                surface->pending_layer_state = surface->initial_layer_state;
                surface->layer_state_pending = 0;
            }
        } else if (!copy_shm_buffer(surface, buffer)) {
            wl_resource_post_error(surface->wl_surface,
                                   WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "unsupported or invalid Wayland SHM buffer");
            return 0;
        } else {
            surface->has_current_buffer = 1;
            wl_buffer_send_release(buffer);
        }
    }
    if (!surface->has_pending_buffer && surface->has_current_buffer &&
        surface->header && !surface->attached &&
        !publish_surface(surface, 0))
        return 0;
    if (layer_state_changed && surface->is_layer_surface &&
        surface->attached) {
        layer_surface_update_position(surface, surface->width,
                                      surface->height);
        if (!publish_layer_surface_role(surface)) {
            fprintf(stderr,
                    "wayland-bridge: layer role update failed for %u\n",
                    surface->surface_id);
            return 0;
        }
        layer_surface_send_configure(surface);
    }
    if (surface->frame_callback) {
        wl_callback_send_done(surface->frame_callback,
                              (uint32_t)(getpid() & UINT32_MAX));
        wl_resource_destroy(surface->frame_callback);
        surface->frame_callback = NULL;
    }
    surface->commit_pending = 0;
    return 1;
}

static void apply_synchronized_subsurface_commits(
    struct bridge_surface *parent) {
    if (!parent || !parent->bridge) return;
    struct bridge *bridge = parent->bridge;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
        struct bridge_surface *child = bridge->surfaces[index];
        if (!child || child->subsurface_parent != parent ||
            !child->is_subsurface)
            continue;
        if (child->has_pending_subsurface_position) {
            child->subsurface_x = child->pending_subsurface_x;
            child->subsurface_y = child->pending_subsurface_y;
            child->has_pending_subsurface_position = 0;
            update_subsurface_position(child);
        }
        if (child->commit_pending &&
            surface_is_effectively_synchronized(child)) {
            if (!apply_surface_commit(child)) continue;
        }
        if (child->has_pending_stack) (void)submit_subsurface_stack(child);
        apply_synchronized_subsurface_commits(child);
    }
}

static void surface_commit(struct wl_client *client,
                           struct wl_resource *resource) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->has_committed = 1;
    if (surface->is_subsurface &&
        surface_is_effectively_synchronized(surface)) {
        surface->commit_pending = 1;
        return;
    }
    if (!apply_surface_commit(surface)) return;
    apply_synchronized_subsurface_commits(surface);
}

static void surface_set_buffer_region(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region) {
    surface_region(client, resource, region);
}

static const struct wl_surface_interface surface_implementation = {
    .destroy = resource_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_set_buffer_region,
    .set_input_region = surface_set_buffer_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_transform,
    .set_buffer_scale = surface_scale,
    .damage_buffer = surface_damage,
    .offset = surface_offset,
};

static void compositor_create_surface(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t id) {
    struct bridge *bridge = wl_resource_get_user_data(resource);
    struct wl_resource *surface_resource = wl_resource_create(
        client, &wl_surface_interface, 4, id);
    struct bridge_surface *surface = calloc(1U, sizeof(*surface));
    if (!surface_resource || !surface) {
        if (surface_resource) wl_resource_destroy(surface_resource);
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    surface->bridge = bridge;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
        if (bridge->surfaces[index]) continue;
        bridge->surfaces[index] = surface;
        break;
    }
    int registered = 0;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index)
        if (bridge->surfaces[index] == surface) registered = 1;
    if (!registered) {
        wl_resource_destroy(surface_resource);
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surface_resource, &surface_implementation,
                                   surface, surface_destroyed);
    surface->wl_surface = surface_resource;
}

static void compositor_create_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
    (void)resource;
    struct wl_resource *region = wl_resource_create(
        client, &wl_region_interface, 1, id);
    if (!region) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region, &region_implementation, NULL, NULL);
}

static const struct wl_compositor_interface compositor_implementation = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

static void bind_compositor(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &wl_compositor_interface,
        version < 4U ? (int)version : 4, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_implementation,
                                   data, NULL);
}

static void subsurface_resource_destroyed(struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->subsurface_resource != resource) return;
    unmap_surface(surface);
    surface->subsurface_resource = NULL;
    surface->subsurface_parent = NULL;
    surface->is_subsurface = 0;
    surface->subsurface_synchronized = 0;
    surface->commit_pending = 0;
    surface->has_pending_subsurface_position = 0;
    surface->has_pending_stack = 0;
    surface->pending_stack_sibling = NULL;
}

static void subsurface_destroy(struct wl_client *client,
                               struct wl_resource *resource) {
    resource_destroy(client, resource);
}

static void subsurface_set_position(struct wl_client *client,
                                    struct wl_resource *resource,
                                    int32_t x, int32_t y) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || !surface->is_subsurface) return;
    surface->pending_subsurface_x = x;
    surface->pending_subsurface_y = y;
    surface->has_pending_subsurface_position = 1;
}

static void subsurface_place(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *sibling_resource,
                             int above) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_surface *sibling = sibling_resource
        ? wl_resource_get_user_data(sibling_resource) : NULL;
    if (!surface || !surface->subsurface_parent || !sibling ||
        sibling == surface || wl_resource_get_client(sibling_resource) != client ||
        (sibling != surface->subsurface_parent &&
         sibling->subsurface_parent != surface->subsurface_parent)) {
        wl_resource_post_error(resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                               "stacking reference must be a sibling or parent surface");
        return;
    }
    surface->pending_stack_sibling = sibling_resource;
    surface->pending_stack_above = above;
    surface->has_pending_stack = 1;
}

static void subsurface_place_above(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling) {
    subsurface_place(client, resource, sibling, 1);
}

static void subsurface_place_below(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling) {
    subsurface_place(client, resource, sibling, 0);
}

static void subsurface_set_sync(struct wl_client *client,
                                struct wl_resource *resource) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) surface->subsurface_synchronized = 1;
}

static void subsurface_set_desync(struct wl_client *client,
                                  struct wl_resource *resource) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    surface->subsurface_synchronized = 0;
    if (surface->commit_pending &&
        !surface_is_effectively_synchronized(surface) &&
        apply_surface_commit(surface))
        apply_synchronized_subsurface_commits(surface);
}

static const struct wl_subsurface_interface subsurface_implementation = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

static void subcompositor_get_subsurface(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         struct wl_resource *surface_resource,
                                         struct wl_resource *parent_resource) {
    struct bridge *bridge = wl_resource_get_user_data(resource);
    struct bridge_surface *surface = surface_resource
        ? wl_resource_get_user_data(surface_resource) : NULL;
    struct bridge_surface *parent = parent_resource
        ? wl_resource_get_user_data(parent_resource) : NULL;
    if (!bridge || !surface || !parent || surface == parent ||
        wl_resource_get_client(surface_resource) != client ||
        wl_resource_get_client(parent_resource) != client ||
        surface->toplevel || surface->popup || surface->is_layer_surface ||
        surface->subsurface_resource ||
        surface->is_cursor) {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "invalid or already-roled sub-surface");
        return;
    }
    for (struct bridge_surface *ancestor = parent; ancestor;
         ancestor = ancestor->subsurface_parent) {
        if (ancestor == surface) {
            wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT,
                                   "sub-surface parent would create a cycle");
            return;
        }
    }

    struct wl_resource *subsurface = wl_resource_create(
        client, &wl_subsurface_interface, 1, id);
    if (!subsurface) {
        wl_client_post_no_memory(client);
        return;
    }
    if (parent->surface_id == 0U) {
        parent->surface_id = allocate_surface_id(bridge);
        if (!parent->toplevel && !parent->popup &&
            !parent->subsurface_resource) {
            parent->is_subsurface_root = 1;
            if (parent->header) {
                parent->attached = 0;
                (void)publish_surface(parent, 0);
            }
        }
    }
    if (surface->surface_id == 0U)
        surface->surface_id = allocate_surface_id(bridge);
    surface->subsurface_resource = subsurface;
    surface->subsurface_parent = parent;
    surface->is_subsurface = 1;
    surface->subsurface_synchronized = 1;
    surface->subsurface_above_parent = 1;
    surface->subsurface_order = parent->next_subsurface_order++;
    surface->subsurface_x = 0;
    surface->subsurface_y = 0;
    surface->x = parent->x;
    surface->y = parent->y;
    if (surface->header) {
        surface->attached = 0;
        surface->commit_pending = 1;
    }
    wl_resource_set_implementation(subsurface, &subsurface_implementation,
                                   surface, subsurface_resource_destroyed);
}

static const struct wl_subcompositor_interface subcompositor_implementation = {
    .destroy = resource_destroy,
    .get_subsurface = subcompositor_get_subsurface,
};

static void bind_subcompositor(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &wl_subcompositor_interface,
        version < 1U ? (int)version : 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &subcompositor_implementation,
                                   data, NULL);
}

static const struct wl_output_interface output_implementation = {
    .release = resource_destroy,
};

static void output_resource_destroyed(struct wl_resource *resource) {
    struct bridge_output *output = wl_resource_get_user_data(resource);
    if (!output) return;
    if (output->bridge) {
        struct bridge_output **link = &output->bridge->outputs;
        while (*link && *link != output) link = &(*link)->next;
        if (*link) *link = output->next;
    }
    free(output);
}

static void move_cursor_surface(struct bridge *bridge,
                                struct bridge_seat *seat) {
    if (!bridge || !seat || !seat->cursor_surface) return;
    struct bridge_surface *surface = seat->cursor_surface;
    int32_t x = bridge->pointer_x - seat->cursor_hotspot_x;
    int32_t y = bridge->pointer_y - seat->cursor_hotspot_y;
    int changed = surface->x != x || surface->y != y;
    surface->x = x;
    surface->y = y;
    if (changed && surface->attached)
        (void)akaos_toolkit_surface_move(&surface->compositor_surface,
                                        surface->x, surface->y);
}

static void pointer_set_cursor(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t serial,
                               struct wl_resource *surface,
                               int32_t hotspot_x, int32_t hotspot_y) {
    struct bridge_seat *seat = wl_resource_get_user_data(resource);
    if (!seat || seat->client != client ||
        serial != seat->last_pointer_enter_serial)
        return;

    struct bridge_surface *cursor = surface
        ? wl_resource_get_user_data(surface) : NULL;
    if (surface && (!cursor || wl_resource_get_client(surface) != client ||
                    cursor->toplevel || cursor->popup || cursor->xdg_surface))
        return;
    if (seat->cursor_surface && seat->cursor_surface != cursor) {
        unmap_surface(seat->cursor_surface);
        seat->cursor_surface->is_cursor = 0;
    }
    seat->cursor_surface = cursor;
    seat->cursor_hotspot_x = hotspot_x;
    seat->cursor_hotspot_y = hotspot_y;
    if (!cursor) return;

    cursor->is_cursor = 1;
    if (cursor->surface_id == 0U) {
        cursor->surface_id = allocate_surface_id(seat->bridge);
        /* A client may commit the cursor buffer before set_cursor. Re-submit
         * that image now that it has a compositor-visible surface identity. */
        cursor->attached = 0;
    }
    move_cursor_surface(seat->bridge, seat);
    if (cursor->header && !cursor->attached)
        (void)publish_surface(cursor, 0);
}

static const struct wl_pointer_interface pointer_implementation = {
    .set_cursor = pointer_set_cursor,
    .release = resource_destroy,
};

static const struct wl_keyboard_interface keyboard_implementation = {
    .release = resource_destroy,
};

static const struct wl_touch_interface touch_implementation = {
    .release = resource_destroy,
};

static void seat_destroyed(struct wl_resource *resource) {
    struct bridge_seat *seat = wl_resource_get_user_data(resource);
    if (!seat) return;
    if (seat->bridge && seat->bridge->moving_seat == seat) {
        seat->bridge->moving_surface = NULL;
        seat->bridge->moving_seat = NULL;
    }
    if (seat->bridge && seat->bridge->resizing_seat == seat) {
        seat->bridge->resizing_surface = NULL;
        seat->bridge->resizing_seat = NULL;
    }
    seat->pressed_button = 0U;
    seat->resource = NULL;
    seat->pointer = NULL;
    seat->keyboard = NULL;
    seat->focused_surface = NULL;
    seat->keyboard_focus_surface = NULL;
    seat->client = NULL;
    seat->bridge = NULL;
}

static void seat_get_pointer(struct wl_client *client,
                             struct wl_resource *resource, uint32_t id) {
    struct bridge_seat *seat = wl_resource_get_user_data(resource);
    int pointer_version = wl_resource_get_version(resource);
    if (pointer_version > 8) pointer_version = 8;
    struct wl_resource *pointer = wl_resource_create(
        client, &wl_pointer_interface, pointer_version, id);
    if (!pointer) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(pointer, &pointer_implementation,
                                   seat, NULL);
    if (seat) seat->pointer = pointer;
}

static void seat_get_keyboard(struct wl_client *client,
                              struct wl_resource *resource, uint32_t id) {
    struct bridge_seat *seat = wl_resource_get_user_data(resource);
    if (!seat || !seat->bridge || seat->bridge->keymap_fd < 0) {
        wl_resource_post_error(resource, WL_SEAT_ERROR_MISSING_CAPABILITY,
                               "keyboard keymap is unavailable");
        return;
    }
    int keyboard_version = wl_resource_get_version(resource);
    struct wl_resource *keyboard = wl_resource_create(
        client, &wl_keyboard_interface, keyboard_version, id);
    if (!keyboard) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(keyboard, &keyboard_implementation,
                                   seat, NULL);
    wl_keyboard_send_keymap(keyboard,
                            WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                            seat->bridge->keymap_fd,
                            seat->bridge->keymap_size);
    if (keyboard_version >= 4)
        wl_keyboard_send_repeat_info(keyboard, 25, 600);
    seat->keyboard = keyboard;
}

static void seat_get_touch(struct wl_client *client,
                           struct wl_resource *resource, uint32_t id) {
    (void)resource;
    struct wl_resource *touch = wl_resource_create(
        client, &wl_touch_interface, 1, id);
    if (!touch) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(touch, &touch_implementation, NULL, NULL);
}

static const struct wl_seat_interface seat_implementation = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = resource_destroy,
};

static void bind_seat(struct wl_client *client, void *data,
                      uint32_t version, uint32_t id) {
    struct bridge *bridge = data;
    struct bridge_seat *seat = NULL;
    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        if (bridge->seats[index].resource) continue;
        seat = &bridge->seats[index];
        memset(seat, 0, sizeof(*seat));
        seat->bridge = bridge;
        seat->client = client;
        break;
    }
    if (!seat) {
        wl_client_post_no_memory(client);
        return;
    }
    int resource_version = version < 5U ? (int)version : 5;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_seat_interface, resource_version, id);
    if (!resource) {
        seat->bridge = NULL;
        seat->client = NULL;
        wl_client_post_no_memory(client);
        return;
    }
    seat->resource = resource;
    wl_resource_set_implementation(resource, &seat_implementation, seat,
                                   seat_destroyed);
    wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER |
                                        WL_SEAT_CAPABILITY_KEYBOARD);
    if (resource_version >= 2)
        wl_seat_send_name(resource, "akaOS Pointer");
}

static struct bridge_surface *find_surface(struct bridge *bridge,
                                           uint32_t surface_id) {
    if (!bridge || surface_id == 0U) return NULL;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
        struct bridge_surface *surface = bridge->surfaces[index];
        if (surface && surface->attached &&
            surface->surface_id == surface_id && surface->wl_surface)
            return surface;
    }
    return NULL;
}

static struct bridge_surface *active_wayland_surface(
    struct bridge *bridge, const struct akaos_compositor_slot **slot_out) {
    if (slot_out) *slot_out = NULL;
    if (!bridge || !bridge->channel) return NULL;
    uint32_t active_id = __atomic_load_n(&bridge->channel->active_surface_id,
                                         __ATOMIC_ACQUIRE);
    struct bridge_surface *surface = find_surface(bridge, active_id);
    if (!surface) return NULL;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        const struct akaos_compositor_slot *slot =
            &bridge->channel->surfaces[index];
        if (__atomic_load_n(&slot->active, __ATOMIC_ACQUIRE) &&
            slot->surface_id == active_id &&
            slot->client_pid == (uint32_t)getpid() &&
            (slot->flags & AKAOS_SURFACE_FLAG_WAYLAND)) {
            if (slot_out) *slot_out = slot;
            return surface;
        }
    }
    return NULL;
}

static uint32_t monotonic_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0U;
    return (uint32_t)((uint64_t)now.tv_sec * 1000U +
                      (uint64_t)now.tv_nsec / 1000000U);
}

static struct bridge_surface *wayland_pointer_target(
    struct bridge *bridge, const struct akaos_wayland_input_event *event,
    int32_t *local_x, int32_t *local_y) {
    if (!bridge || !bridge->channel || !event || !local_x || !local_y)
        return NULL;
    const struct akaos_compositor_slot *target_slot = NULL;
    uint32_t target_z = 0U;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        const struct akaos_compositor_slot *slot =
            &bridge->channel->surfaces[index];
        if (!slot->active || slot->client_pid != (uint32_t)getpid() ||
            !(slot->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
            (slot->flags & AKAOS_SURFACE_FLAG_CURSOR) ||
            event->pointer_x < slot->x || event->pointer_y < slot->y ||
            event->pointer_x >= slot->x + (int32_t)slot->width ||
            event->pointer_y >= slot->y + (int32_t)slot->height ||
            (target_slot && slot->z_order < target_z))
            continue;
        target_slot = slot;
        target_z = slot->z_order;
    }
    if (!target_slot) return NULL;
    struct bridge_surface *surface = find_surface(
        bridge, target_slot->surface_id);
    if (!surface) return NULL;
    *local_x = event->pointer_x - target_slot->x;
    *local_y = event->pointer_y - target_slot->y;
    return surface;
}

static int surface_is_within_popup(
    const struct bridge_surface *surface,
    const struct bridge_surface *popup) {
    for (size_t depth = 0; surface && depth < WAYLAND_SURFACE_LIMIT; ++depth) {
        if (surface == popup) return 1;
        if (surface->subsurface_parent)
            surface = surface->subsurface_parent;
        else
            surface = surface->popup_parent;
    }
    return 0;
}

static struct bridge_surface *popup_stack_target(
    struct bridge_surface *grabbed_popup,
    struct bridge_surface *pointer_target) {
    for (struct bridge_surface *popup = grabbed_popup;
         popup && popup->popup; popup = popup->popup_parent) {
        if (surface_is_within_popup(pointer_target, popup)) return popup;
    }
    return NULL;
}

static void send_keyboard_focus(struct bridge *bridge,
                                struct bridge_seat *seat,
                                struct bridge_surface *target) {
    if (!bridge || !seat || !seat->keyboard) return;
    if (target && (!target->wl_surface ||
                   wl_resource_get_client(target->wl_surface) != seat->client))
        target = NULL;
    struct bridge_surface *previous = seat->keyboard_focus_surface
        ? wl_resource_get_user_data(seat->keyboard_focus_surface) : NULL;
    struct wl_resource *keyboard_surface =
        target ? target->wl_surface : NULL;
    if (seat->keyboard_focus_surface == keyboard_surface) return;

    uint32_t serial = wl_display_next_serial(bridge->display);
    if (seat->keyboard_focus_surface)
        wl_keyboard_send_leave(seat->keyboard, serial,
                               seat->keyboard_focus_surface);
    seat->keyboard_focus_surface = keyboard_surface;
    if (!keyboard_surface) {
        if (previous && previous->toplevel && previous->attached &&
            previous->configured && !previous->resize_configure_pending)
            (void)send_resize_configure(previous, previous->x, previous->y,
                                        previous->width, previous->height, 1);
        foreign_toplevel_send_state(previous);
        return;
    }

    struct wl_array pressed_keys;
    wl_array_init(&pressed_keys);
    wl_keyboard_send_enter(seat->keyboard, serial, keyboard_surface,
                           &pressed_keys);
    wl_array_release(&pressed_keys);
    xkb_mod_mask_t depressed = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_DEPRESSED) : 0U;
    xkb_mod_mask_t latched = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_LATCHED) : 0U;
    xkb_mod_mask_t locked = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_LOCKED) : 0U;
    xkb_layout_index_t group = bridge->xkb_state
        ? xkb_state_serialize_layout(bridge->xkb_state,
                                     XKB_STATE_LAYOUT_EFFECTIVE) : 0U;
    wl_keyboard_send_modifiers(seat->keyboard, serial, depressed, latched,
                               locked, (uint32_t)group);
    if (previous && previous->toplevel && previous->attached &&
        previous->configured && !previous->resize_configure_pending)
        (void)send_resize_configure(previous, previous->x, previous->y,
                                    previous->width, previous->height, 1);
    if (target && target != previous && target->toplevel && target->attached &&
        target->configured && !target->resize_configure_pending)
        (void)send_resize_configure(target, target->x, target->y,
                                    target->width, target->height, 1);
    foreign_toplevel_send_state(previous);
    foreign_toplevel_send_state(target);
}

static void deliver_wayland_pointer_event(
    struct bridge *bridge, const struct akaos_wayland_input_event *event) {
    if (!bridge || !event) return;
    bridge->pointer_x = event->pointer_x;
    bridge->pointer_y = event->pointer_y;
    int pointer_motion =
        (event->type == AKAOS_EV_REL &&
         (event->code == AKAOS_REL_X || event->code == AKAOS_REL_Y)) ||
        (event->type == AKAOS_EV_ABS &&
         (event->code == AKAOS_ABS_X || event->code == AKAOS_ABS_Y));
    if (pointer_motion && bridge->moving_surface && bridge->moving_seat &&
        bridge->moving_seat->pressed_button != 0U) {
        struct bridge_surface *moving = bridge->moving_surface;
        int32_t x = bridge->pointer_x - bridge->move_offset_x;
        int32_t y = bridge->pointer_y - bridge->move_offset_y;
        if (moving->x != x || moving->y != y) {
            moving->x = x;
            moving->y = y;
            if (moving->attached)
                (void)akaos_toolkit_surface_move(&moving->compositor_surface,
                                                  x, y);
        }
    }
    if (pointer_motion && bridge->resizing_surface && bridge->resizing_seat &&
        bridge->resizing_seat->pressed_button == AKAOS_BTN_LEFT)
        update_interactive_resize(bridge);
    if (event->type == AKAOS_EV_KEY && event->code == AKAOS_KEY_ESC) {
        if (bridge->escape_key_suppressed) {
            if (event->value == 0) bridge->escape_key_suppressed = 0;
            return;
        }
        if (event->value != 0 && bridge->grabbed_popup) {
            bridge->escape_key_suppressed = 1;
            dismiss_grabbed_popup(bridge);
            return;
        }
    }
    if (event->type == AKAOS_EV_KEY && event->code >= AKAOS_BTN_LEFT &&
        event->value == 0) {
        for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
            struct bridge_seat *seat = &bridge->seats[index];
            if (seat->pressed_button != event->code) continue;
            seat->pressed_button = 0U;
            if (bridge->moving_seat == seat) {
                bridge->moving_surface = NULL;
                bridge->moving_seat = NULL;
            }
            if (bridge->resizing_seat == seat) {
                bridge->resizing_surface = NULL;
                bridge->resizing_seat = NULL;
            }
        }
    }
    int32_t local_x = 0;
    int32_t local_y = 0;
    struct bridge_surface *pointer_target = wayland_pointer_target(
        bridge, event, &local_x, &local_y);
    const char *debug = getenv("AKAOS_XKB_DEBUG");
    if (event->type == AKAOS_EV_KEY && event->code == AKAOS_BTN_LEFT &&
        event->value != 0 && debug && debug[0] && debug[0] != '0') {
        fprintf(stderr,
                "wayland-bridge: pointer press screen=%d,%d target=%u local=%d,%d\n",
                event->pointer_x, event->pointer_y,
                pointer_target ? pointer_target->surface_id : 0U,
                local_x, local_y);
        fflush(stderr);
    }
    if (event->type == AKAOS_EV_KEY && event->code >= AKAOS_BTN_LEFT &&
        event->value != 0 && bridge->grabbed_popup) {
        struct bridge_surface *retained_popup = popup_stack_target(
            bridge->grabbed_popup, pointer_target);
        if (retained_popup != bridge->grabbed_popup)
            dismiss_grabbed_popup_to(bridge, retained_popup);
    }
    struct bridge_surface *keyboard_target = bridge->grabbed_popup &&
        bridge->grabbed_popup->attached ? bridge->grabbed_popup
                                        : active_wayland_surface(bridge, NULL);
    uint32_t time = monotonic_milliseconds();
    int keyboard_event = event->type == AKAOS_EV_KEY &&
                         event->code < AKAOS_BTN_LEFT;
    if (keyboard_event && bridge->xkb_state) {
        xkb_keycode_t xkb_code = (xkb_keycode_t)event->code + 8U;
        enum xkb_key_direction direction = event->value == 0
            ? XKB_KEY_UP : XKB_KEY_DOWN;
        (void)xkb_state_update_key(bridge->xkb_state, xkb_code, direction);
    }
    xkb_mod_mask_t depressed = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_DEPRESSED) : 0U;
    xkb_mod_mask_t latched = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_LATCHED) : 0U;
    xkb_mod_mask_t locked = bridge->xkb_state
        ? xkb_state_serialize_mods(bridge->xkb_state,
                                   XKB_STATE_MODS_LOCKED) : 0U;
    xkb_layout_index_t group = bridge->xkb_state
        ? xkb_state_serialize_layout(bridge->xkb_state,
                                     XKB_STATE_LAYOUT_EFFECTIVE) : 0U;

    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        struct bridge_seat *seat = &bridge->seats[index];
        if (!seat->resource) continue;

        struct bridge_surface *seat_pointer_target = pointer_target;
        if (seat_pointer_target &&
            wl_resource_get_client(seat_pointer_target->wl_surface) !=
                seat->client)
            seat_pointer_target = NULL;
        struct wl_resource *pointer_resource = seat_pointer_target
            ? seat_pointer_target->wl_surface : NULL;
        if (seat->pointer && seat->focused_surface != pointer_resource) {
            uint32_t serial = wl_display_next_serial(bridge->display);
            if (pointer_resource) seat->last_pointer_enter_serial = serial;
            if (seat->focused_surface)
                wl_pointer_send_leave(seat->pointer, serial,
                                      seat->focused_surface);
            seat->focused_surface = pointer_resource;
            if (pointer_resource) {
                wl_pointer_send_enter(seat->pointer, serial,
                                      pointer_resource,
                                      wl_fixed_from_int(local_x),
                                      wl_fixed_from_int(local_y));
            }
        }

        send_keyboard_focus(bridge, seat, keyboard_target);
        move_cursor_surface(bridge, seat);
        struct wl_resource *keyboard_resource =
            seat->keyboard_focus_surface;

        if (seat->keyboard && keyboard_resource && keyboard_event) {
            uint32_t key_state = event->value == 0
                ? WL_KEYBOARD_KEY_STATE_RELEASED
                : WL_KEYBOARD_KEY_STATE_PRESSED;
            uint32_t serial = wl_display_next_serial(bridge->display);
            wl_keyboard_send_key(seat->keyboard, serial, time, event->code,
                                 key_state);
            wl_keyboard_send_modifiers(seat->keyboard, serial, depressed,
                                       latched, locked, (uint32_t)group);
        }
        if (!seat->pointer || !pointer_resource) continue;

        int pointer_motion =
            (event->type == AKAOS_EV_REL &&
             (event->code == AKAOS_REL_X || event->code == AKAOS_REL_Y)) ||
            (event->type == AKAOS_EV_ABS &&
             (event->code == AKAOS_ABS_X || event->code == AKAOS_ABS_Y));
        if (pointer_motion) {
            wl_pointer_send_motion(seat->pointer, time,
                                   wl_fixed_from_int(local_x),
                                   wl_fixed_from_int(local_y));
        } else if (event->type == AKAOS_EV_KEY &&
                   event->code >= AKAOS_BTN_LEFT) {
            uint32_t state = event->value == 0
                ? WL_POINTER_BUTTON_STATE_RELEASED
                : WL_POINTER_BUTTON_STATE_PRESSED;
            uint32_t serial = wl_display_next_serial(bridge->display);
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                seat->last_pointer_press_serial = serial;
                seat->pressed_button = event->code;
                seat->last_pointer_press_x = event->pointer_x;
                seat->last_pointer_press_y = event->pointer_y;
            }
            wl_pointer_send_button(seat->pointer, serial, time, event->code,
                                   state);
        }
        if (wl_resource_get_version(seat->pointer) >= 5)
            wl_pointer_send_frame(seat->pointer);
    }
}

static void sync_subsurface_positions(struct bridge *bridge) {
    if (!bridge || !bridge->channel) return;
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
        struct bridge_surface *root = bridge->surfaces[index];
        if (!root || root->subsurface_parent || !root->attached ||
            root->surface_id == 0U)
            continue;
        for (size_t slot_index = 0;
             slot_index < AKAOS_COMPOSITOR_MAX_SURFACES; ++slot_index) {
            const struct akaos_compositor_slot *slot =
                &bridge->channel->surfaces[slot_index];
            if (slot->active && slot->surface_id == root->surface_id &&
                slot->client_pid == (uint32_t)getpid()) {
                root->x = slot->x;
                root->y = slot->y;
                root->z_order = slot->z_order;
                break;
            }
        }
    }
    for (size_t pass = 0; pass < WAYLAND_SURFACE_LIMIT; ++pass) {
        for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index) {
            struct bridge_surface *surface = bridge->surfaces[index];
            if (surface && surface->is_subsurface)
                update_subsurface_position(surface);
        }
    }
}

static int dispatch_wayland_input(void *data) {
    struct bridge *bridge = data;
    sync_subsurface_positions(bridge);
    struct akaos_wayland_input_event event;
    while (akaos_wayland_input_receive(bridge->channel, &event))
        deliver_wayland_pointer_event(bridge, &event);
    sync_subsurface_positions(bridge);
    return wl_event_source_timer_update(bridge->input_timer, 8U);
}

static void bind_output_with_release(struct wl_client *client, void *data,
                                     uint32_t version, uint32_t id) {
    struct bridge *bridge = data;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_output_interface, version < 2U ? (int)version : 2, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    struct bridge_output *output = calloc(1U, sizeof(*output));
    if (!output) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    output->bridge = bridge;
    output->resource = resource;
    output->next = bridge->outputs;
    bridge->outputs = output;
    wl_resource_set_implementation(resource, &output_implementation,
                                   output, output_resource_destroyed);
    wl_output_send_geometry(resource, 0, 0, 600, 340,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN, "akaOS", "Lumen",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(resource,
                        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        (int32_t)bridge->display_info.width,
                        (int32_t)bridge->display_info.height, 60000);
    wl_output_send_scale(resource, 1);
    if (version >= 2U) wl_output_send_done(resource);
}

static void data_source_offer(struct wl_client *client,
                              struct wl_resource *resource,
                              const char *mime_type) {
    (void)client; (void)resource; (void)mime_type;
}

static void data_source_actions(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t actions) {
    (void)client; (void)resource; (void)actions;
}

static const struct wl_data_source_interface data_source_implementation = {
    .offer = data_source_offer,
    .destroy = resource_destroy,
    .set_actions = data_source_actions,
};

static void data_device_start_drag(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *source,
                                   struct wl_resource *origin,
                                   struct wl_resource *icon,
                                   uint32_t serial) {
    (void)client; (void)resource; (void)source; (void)origin;
    (void)icon; (void)serial;
}

static void data_device_set_selection(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *source,
                                      uint32_t serial) {
    (void)client; (void)resource; (void)source; (void)serial;
}

static const struct wl_data_device_interface data_device_implementation = {
    .start_drag = data_device_start_drag,
    .set_selection = data_device_set_selection,
    .release = resource_destroy,
};

static void data_manager_create_source(struct wl_client *client,
                                       struct wl_resource *resource,
                                       uint32_t id) {
    (void)resource;
    struct wl_resource *source = wl_resource_create(
        client, &wl_data_source_interface, 1, id);
    if (!source) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(source, &data_source_implementation,
                                   NULL, NULL);
}

static void data_manager_get_device(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t id,
                                    struct wl_resource *seat_resource) {
    (void)resource;
    (void)seat_resource;
    struct wl_resource *device = wl_resource_create(
        client, &wl_data_device_interface, 1, id);
    if (!device) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(device, &data_device_implementation,
                                   NULL, NULL);
}

static const struct wl_data_device_manager_interface data_manager_implementation = {
    .create_data_source = data_manager_create_source,
    .get_data_device = data_manager_get_device,
    .release = resource_destroy,
};

static void bind_data_manager(struct wl_client *client, void *data,
                              uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &wl_data_device_manager_interface,
        version < 1U ? (int)version : 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &data_manager_implementation,
                                   data, NULL);
}

static void xdg_positioner_destroy(struct wl_client *client,
                                   struct wl_resource *resource) {
    resource_destroy(client, resource);
}

static void positioner_destroyed(struct wl_resource *resource) {
    free(wl_resource_get_user_data(resource));
}

static void positioner_size(struct wl_client *client,
                            struct wl_resource *resource,
                            int32_t width, int32_t height) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    if (width <= 0 || height <= 0 ||
        width > (int32_t)WAYLAND_MAX_DIMENSION ||
        height > (int32_t)WAYLAND_MAX_DIMENSION) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "popup size is outside the supported range");
        return;
    }
    positioner->width = width;
    positioner->height = height;
    positioner->has_size = 1;
}

static void positioner_anchor_rect(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t x, int32_t y,
                                   int32_t width, int32_t height) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    if (width < 0 || height < 0 ||
        x < -(int32_t)WAYLAND_MAX_DIMENSION ||
        y < -(int32_t)WAYLAND_MAX_DIMENSION ||
        x > (int32_t)WAYLAND_MAX_DIMENSION ||
        y > (int32_t)WAYLAND_MAX_DIMENSION ||
        width > (int32_t)WAYLAND_MAX_DIMENSION ||
        height > (int32_t)WAYLAND_MAX_DIMENSION ||
        x > INT32_MAX - width || y > INT32_MAX - height) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup anchor rectangle");
        return;
    }
    positioner->anchor_x = x;
    positioner->anchor_y = y;
    positioner->anchor_width = width;
    positioner->anchor_height = height;
    positioner->has_anchor_rect = 1;
}

static void positioner_anchor(struct wl_client *client,
                              struct wl_resource *resource, uint32_t anchor) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    if (!xdg_positioner_anchor_is_valid(anchor, 1U)) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup anchor");
        return;
    }
    positioner->anchor = anchor;
}

static void positioner_gravity(struct wl_client *client,
                               struct wl_resource *resource, uint32_t gravity) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    if (!xdg_positioner_gravity_is_valid(gravity, 1U)) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup gravity");
        return;
    }
    positioner->gravity = gravity;
}

static void positioner_constraints(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t adjustment) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    if (!xdg_positioner_constraint_adjustment_is_valid(adjustment, 1U)) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup constraint adjustment");
        return;
    }
    positioner->constraint_adjustment = adjustment;
}

static void positioner_offset(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y) {
    (void)client;
    struct bridge_positioner *positioner = wl_resource_get_user_data(resource);
    if (!positioner) return;
    positioner->offset_x = x;
    positioner->offset_y = y;
}

static void positioner_reactive(struct wl_client *client,
                                struct wl_resource *resource) {
    (void)client; (void)resource;
}

static void positioner_parent_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t width, int32_t height) {
    (void)client; (void)resource; (void)width; (void)height;
}

static void positioner_parent_configure(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t serial) {
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_positioner_interface positioner_implementation = {
    .destroy = xdg_positioner_destroy,
    .set_size = positioner_size,
    .set_anchor_rect = positioner_anchor_rect,
    .set_anchor = positioner_anchor,
    .set_gravity = positioner_gravity,
    .set_constraint_adjustment = positioner_constraints,
    .set_offset = positioner_offset,
    .set_reactive = positioner_reactive,
    .set_parent_size = positioner_parent_size,
    .set_parent_configure = positioner_parent_configure,
};

static void wm_base_create_positioner(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
    (void)resource;
    struct wl_resource *positioner = wl_resource_create(
        client, &xdg_positioner_interface, 1, id);
    struct bridge_positioner *data = calloc(1U, sizeof(*data));
    if (!positioner || !data) {
        if (positioner) wl_resource_destroy(positioner);
        free(data);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(positioner, &positioner_implementation,
                                   data, positioner_destroyed);
}

static void xdg_surface_destroyed(struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) surface->xdg_surface = NULL;
}

static void wm_base_get_xdg_surface(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t id,
                                    struct wl_resource *surface_resource) {
    struct bridge_surface *surface =
        wl_resource_get_user_data(surface_resource);
    if (!surface || surface->is_layer_surface) {
        wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
                               "xdg_surface requires a bridge surface");
        return;
    }
    struct wl_resource *xdg_resource = wl_resource_create(
        client, &xdg_surface_interface, 1, id);
    if (!xdg_resource) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->xdg_surface = xdg_resource;
    wl_resource_set_implementation(xdg_resource, &xdg_surface_implementation,
                                   surface, xdg_surface_destroyed);
}

static void wm_base_pong(struct wl_client *client,
                         struct wl_resource *resource, uint32_t serial) {
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_wm_base_interface wm_base_implementation = {
    .destroy = resource_destroy,
    .create_positioner = wm_base_create_positioner,
    .get_xdg_surface = wm_base_get_xdg_surface,
    .pong = wm_base_pong,
};

static void bind_wm_base(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *resource = wl_resource_create(
        client, &xdg_wm_base_interface, version < 1U ? (int)version : 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &wm_base_implementation,
                                   NULL, NULL);
}

static void layer_surface_resource_destroyed(struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    unmap_surface(surface);
    if (surface->layer_surface == resource) surface->layer_surface = NULL;
    surface->configured = 0;
    surface->configure_serial = 0U;
    surface->acknowledged_configure_serial = 0U;
}

static void layer_shell_get_layer_surface(
    struct wl_client *client, struct wl_resource *resource, uint32_t id,
    struct wl_resource *surface_resource, struct wl_resource *output_resource,
    uint32_t layer, const char *namespace_name) {
    struct bridge *bridge = wl_resource_get_user_data(resource);
    struct bridge_surface *surface = surface_resource
        ? wl_resource_get_user_data(surface_resource) : NULL;
    if (!bridge || !surface || surface->bridge != bridge ||
        wl_resource_get_client(surface_resource) != client) {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "layer surface references an invalid wl_surface");
        return;
    }
    if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
                               "invalid layer-shell layer");
        return;
    }
    if (output_resource &&
        wl_resource_get_client(output_resource) != client) {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "layer surface output belongs to another client");
        return;
    }
    if (surface->has_committed || surface->has_pending_buffer ||
        surface->has_current_buffer || surface->header) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
                               "layer surface wl_surface was already committed");
        return;
    }
    if (surface->is_layer_surface || surface->xdg_surface ||
        surface->toplevel || surface->popup || surface->is_subsurface ||
        surface->subsurface_resource || surface->is_cursor ||
        surface->surface_id != 0U) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
                               "wl_surface already has a role");
        return;
    }

    struct wl_resource *layer_surface = wl_resource_create(
        client, &zwlr_layer_surface_v1_interface,
        wl_resource_get_version(resource), id);
    if (!layer_surface) {
        wl_client_post_no_memory(client);
        return;
    }

    memset(&surface->initial_layer_state, 0,
           sizeof(surface->initial_layer_state));
    surface->initial_layer_state.layer = layer;
    surface->layer_state = surface->initial_layer_state;
    surface->pending_layer_state = surface->initial_layer_state;
    surface->layer_state_pending = 0;
    surface->is_layer_surface = 1;
    surface->layer_surface = layer_surface;
    surface->surface_id = allocate_surface_id(bridge);
    copy_text(surface->title, sizeof(surface->title), namespace_name);
    surface->x = 0;
    surface->y = 0;
    fprintf(stderr, "wayland-bridge: layer-shell role namespace=%s layer=%u\n",
            surface->title, layer);
    wl_resource_set_implementation(layer_surface,
                                   &layer_surface_implementation,
                                   surface,
                                   layer_surface_resource_destroyed);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_implementation = {
    .get_layer_surface = layer_shell_get_layer_surface,
    .destroy = resource_destroy,
};

static void bind_layer_shell(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &zwlr_layer_shell_v1_interface,
        version < 4U ? (int)version : 4, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &layer_shell_implementation,
                                   data, NULL);
}

static void layer_surface_set_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t width, uint32_t height) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (width > WAYLAND_MAX_DIMENSION || height > WAYLAND_MAX_DIMENSION) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
                               "layer surface size exceeds the supported range");
        return;
    }
    layer_surface_begin_state(surface);
    surface->pending_layer_state.width = width;
    surface->pending_layer_state.height = height;
}

static void layer_surface_set_anchor(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t anchor) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    const uint32_t valid = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (!surface) return;
    if (anchor & ~valid) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR,
                               "invalid layer surface anchor bits");
        return;
    }
    layer_surface_begin_state(surface);
    surface->pending_layer_state.anchor = anchor;
}

static void layer_surface_set_exclusive_zone(struct wl_client *client,
                                             struct wl_resource *resource,
                                             int32_t zone) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    layer_surface_begin_state(surface);
    surface->pending_layer_state.exclusive_zone = zone;
}

static void layer_surface_set_margin(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t top, int32_t right,
                                     int32_t bottom, int32_t left) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    layer_surface_begin_state(surface);
    surface->pending_layer_state.margin_top = top;
    surface->pending_layer_state.margin_right = right;
    surface->pending_layer_state.margin_bottom = bottom;
    surface->pending_layer_state.margin_left = left;
}

static void layer_surface_set_keyboard_interactivity(
    struct wl_client *client, struct wl_resource *resource,
    uint32_t keyboard_interactivity) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (keyboard_interactivity >
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND) {
        wl_resource_post_error(
            resource,
            ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY,
            "invalid layer surface keyboard interactivity");
        return;
    }
    layer_surface_begin_state(surface);
    surface->pending_layer_state.keyboard_interactivity =
        keyboard_interactivity;
}

static void layer_surface_get_popup(struct wl_client *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *popup_resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_surface *popup = popup_resource
        ? wl_resource_get_user_data(popup_resource) : NULL;
    if (!surface || !popup || popup->popup != popup_resource ||
        popup->popup_parent || wl_resource_get_client(popup_resource) != client) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                               "layer popup is invalid or already parented");
        return;
    }
    popup->popup_parent = surface;
}

static void layer_surface_ack_configure(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t serial) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || !surface->configured || serial == 0U ||
        serial != surface->configure_serial) {
        wl_resource_post_error(resource,
                               ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                               "layer surface acknowledged an unknown configure");
        return;
    }
    surface->acknowledged_configure_serial = serial;
}

static void layer_surface_destroy(struct wl_client *client,
                                  struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) unmap_surface(surface);
    resource_destroy(client, resource);
}

static void layer_surface_set_layer(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t layer) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        wl_resource_post_error(surface->layer_surface,
                               ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
                               "invalid layer-shell layer");
        return;
    }
    layer_surface_begin_state(surface);
    surface->pending_layer_state.layer = layer;
}

static const struct zwlr_layer_surface_v1_interface layer_surface_implementation = {
    .set_size = layer_surface_set_size,
    .set_anchor = layer_surface_set_anchor,
    .set_exclusive_zone = layer_surface_set_exclusive_zone,
    .set_margin = layer_surface_set_margin,
    .set_keyboard_interactivity = layer_surface_set_keyboard_interactivity,
    .get_popup = layer_surface_get_popup,
    .ack_configure = layer_surface_ack_configure,
    .destroy = layer_surface_destroy,
    .set_layer = layer_surface_set_layer,
};

static void xdg_surface_destroy(struct wl_client *client,
                                struct wl_resource *resource) {
    resource_destroy(client, resource);
}

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (surface->toplevel || surface->popup || surface->is_layer_surface) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "xdg surface already has a role");
        return;
    }
    if (surface->surface_id == 0U) {
        surface->surface_id = allocate_surface_id(surface->bridge);
        surface->x = surface->bridge->display_info.width > 640U
            ? (int32_t)(surface->bridge->display_info.width - 640U) / 2 : 0;
        surface->y = surface->bridge->display_info.height > 480U
            ? (int32_t)(surface->bridge->display_info.height - 480U) / 2 : 0;
    }
    struct wl_resource *toplevel = wl_resource_create(
        client, &xdg_toplevel_interface, 1, id);
    if (!toplevel) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->toplevel = toplevel;
    wl_resource_set_implementation(toplevel, &xdg_toplevel_implementation,
                                   surface, NULL);
}

static void restore_popup_focus(struct bridge_surface *surface) {
    if (!surface || !surface->bridge) return;
    struct bridge *bridge = surface->bridge;
    if (bridge->grabbed_popup == surface) bridge->grabbed_popup = NULL;
    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        struct bridge_seat *seat = &bridge->seats[index];
        if (seat->keyboard_focus_surface == surface->wl_surface)
            send_keyboard_focus(bridge, seat, surface->popup_parent);
    }
}

static void dismiss_grabbed_popup_to(
    struct bridge *bridge, struct bridge_surface *retained_popup) {
    if (!bridge || !bridge->grabbed_popup) return;
    struct bridge_surface *popup = bridge->grabbed_popup;
    struct wl_client *client = popup->wl_surface
        ? wl_resource_get_client(popup->wl_surface) : NULL;
    struct bridge_surface *focus = retained_popup;
    if (!focus) {
        focus = popup->popup_parent;
        while (focus && focus->popup) focus = focus->popup_parent;
    }

    /* Remove the active popup descendants above the retained popup (if any).
     * Unmap immediately while the client handles popup_done and destroys its
     * xdg_popup resources. */
    for (struct bridge_surface *current = popup;
         current && current != retained_popup && current->popup;
         current = current->popup_parent) {
        xdg_popup_send_popup_done(current->popup);
        unmap_surface(current);
    }
    bridge->grabbed_popup = retained_popup;
    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        struct bridge_seat *seat = &bridge->seats[index];
        if (seat->resource && seat->client == client)
            send_keyboard_focus(bridge, seat, focus);
    }
}

static void dismiss_grabbed_popup(struct bridge *bridge) {
    dismiss_grabbed_popup_to(bridge, NULL);
}

static void xdg_popup_resource_destroyed(struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    restore_popup_focus(surface);
    if (surface->popup == resource) surface->popup = NULL;
}

static void xdg_popup_destroy(struct wl_client *client,
                              struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        unmap_surface(surface);
        surface->configured = 0;
        restore_popup_focus(surface);
    }
    resource_destroy(client, resource);
}

static void xdg_popup_grab(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *seat_resource,
                           uint32_t serial) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_seat *seat = seat_resource
        ? wl_resource_get_user_data(seat_resource) : NULL;
    if (!surface || !surface->bridge || !seat ||
        seat->client != client || wl_resource_get_client(seat_resource) != client ||
        !seat->last_pointer_press_serial ||
        serial != seat->last_pointer_press_serial) {
        wl_resource_post_error(resource, XDG_POPUP_ERROR_INVALID_GRAB,
                               "popup grab does not match a recent pointer press");
        return;
    }
    if (surface->popup_parent && surface->popup_parent->popup &&
        surface->bridge->grabbed_popup != surface->popup_parent) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "nested popup parent does not own the grab");
        return;
    }
    surface->bridge->grabbed_popup = surface;
    send_keyboard_focus(surface->bridge, seat, surface);
}

static void xdg_popup_reposition(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *positioner_resource,
                                 uint32_t token) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_positioner *positioner = positioner_resource
        ? wl_resource_get_user_data(positioner_resource) : NULL;
    if (!surface || !surface->bridge || !positioner ||
        !positioner->has_size || !positioner->has_anchor_rect) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "popup reposition requires a complete positioner");
        return;
    }
    position_popup(positioner, surface->bridge,
                   &surface->popup_x, &surface->popup_y);
    surface->popup_width = (uint32_t)positioner->width;
    surface->popup_height = (uint32_t)positioner->height;
    if (surface->popup_parent) {
        int64_t popup_screen_x = (int64_t)surface->popup_parent->x +
            surface->popup_parent->window_geometry_x + surface->popup_x;
        int64_t popup_screen_y = (int64_t)surface->popup_parent->y +
            surface->popup_parent->window_geometry_y + surface->popup_y;
        if (popup_screen_x > INT32_MAX) popup_screen_x = INT32_MAX;
        if (popup_screen_x < INT32_MIN) popup_screen_x = INT32_MIN;
        if (popup_screen_y > INT32_MAX) popup_screen_y = INT32_MAX;
        if (popup_screen_y < INT32_MIN) popup_screen_y = INT32_MIN;
        surface->x = (int32_t)popup_screen_x;
        surface->y = (int32_t)popup_screen_y;
    }
    if (surface->attached) {
        (void)akaos_toolkit_surface_move(&surface->compositor_surface,
                                         surface->x, surface->y);
    }
    if (wl_resource_get_version(resource) >= 3)
        xdg_popup_send_repositioned(resource, token);
    xdg_popup_send_configure(resource, surface->popup_x, surface->popup_y,
                             (int32_t)surface->popup_width,
                             (int32_t)surface->popup_height);
    surface->configure_serial =
        wl_display_next_serial(surface->bridge->display);
    xdg_surface_send_configure(surface->xdg_surface,
                               surface->configure_serial);
}

static const struct xdg_popup_interface xdg_popup_implementation = {
    .destroy = xdg_popup_destroy,
    .grab = xdg_popup_grab,
    .reposition = xdg_popup_reposition,
};

static void xdg_surface_get_popup(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *parent_resource,
                                  struct wl_resource *positioner_resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_surface *parent = parent_resource
        ? wl_resource_get_user_data(parent_resource) : NULL;
    struct bridge_positioner *positioner = positioner_resource
        ? wl_resource_get_user_data(positioner_resource) : NULL;
    if (!surface || surface->toplevel || surface->popup) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "xdg surface already has a role");
        return;
    }
    if (!parent_resource || !positioner_resource || !parent ||
        parent == surface || !parent->xdg_surface ||
        (!parent->toplevel && !parent->popup) ||
        wl_resource_get_client(parent_resource) != client ||
        wl_resource_get_client(positioner_resource) != client) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "popup parent must be a constructed xdg surface");
        return;
    }
    if (!positioner || !positioner->has_size ||
        !positioner->has_anchor_rect) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_INVALID_SIZE,
                               "popup positioner requires size and anchor rectangle");
        return;
    }

    struct wl_resource *popup = wl_resource_create(
        client, &xdg_popup_interface, 1, id);
    if (!popup) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->surface_id = allocate_surface_id(surface->bridge);
    surface->popup_parent = parent;
    surface->popup_x = 0;
    surface->popup_y = 0;
    position_popup(positioner, surface->bridge,
                   &surface->popup_x, &surface->popup_y);
    surface->popup_width = (uint32_t)positioner->width;
    surface->popup_height = (uint32_t)positioner->height;
    int64_t popup_screen_x = (int64_t)parent->x +
                             parent->window_geometry_x + surface->popup_x;
    int64_t popup_screen_y = (int64_t)parent->y +
                             parent->window_geometry_y + surface->popup_y;
    if (popup_screen_x > INT32_MAX) popup_screen_x = INT32_MAX;
    if (popup_screen_x < INT32_MIN) popup_screen_x = INT32_MIN;
    if (popup_screen_y > INT32_MAX) popup_screen_y = INT32_MAX;
    if (popup_screen_y < INT32_MIN) popup_screen_y = INT32_MIN;
    surface->x = (int32_t)popup_screen_x;
    surface->y = (int32_t)popup_screen_y;
    surface->popup = popup;
    wl_resource_set_implementation(popup, &xdg_popup_implementation,
                                   surface, xdg_popup_resource_destroyed);
}

static void xdg_surface_geometry(struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
    (void)client;
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_INVALID_SIZE,
                               "window geometry dimensions must be positive");
        return;
    }
    surface->window_geometry_x = x;
    surface->window_geometry_y = y;
    surface->window_geometry_width = (uint32_t)width;
    surface->window_geometry_height = (uint32_t)height;
}

static void xdg_surface_ack_configure(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t serial) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface && surface->configure_serial == serial) {
        surface->configured = 1;
        surface->acknowledged_configure_serial = serial;
    }
    (void)client;
}

static const struct xdg_surface_interface xdg_surface_implementation = {
    .destroy = xdg_surface_destroy,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

static void toplevel_destroy(struct wl_client *client,
                             struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        foreign_toplevel_unmap(surface);
        surface->toplevel = NULL;
    }
    resource_destroy(client, resource);
}

static void toplevel_parent(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *parent) {
    (void)client; (void)resource; (void)parent;
}

static void toplevel_title(struct wl_client *client,
                           struct wl_resource *resource, const char *title) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        copy_text(surface->title, sizeof(surface->title), title);
        foreign_toplevel_send_metadata(surface);
    }
    (void)client;
}

static void toplevel_app_id(struct wl_client *client,
                            struct wl_resource *resource, const char *app_id) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        copy_text(surface->app_id, sizeof(surface->app_id), app_id);
        foreign_toplevel_send_metadata(surface);
    }
    (void)client;
}

static void toplevel_menu(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial,
                          int32_t x, int32_t y) {
    (void)client; (void)resource; (void)seat; (void)serial; (void)x; (void)y;
}

static void toplevel_move(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *seat_resource,
                          uint32_t serial) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_seat *seat = seat_resource
        ? wl_resource_get_user_data(seat_resource) : NULL;
    if (!surface || !surface->bridge || surface->toplevel != resource ||
        !surface->attached || !seat || seat->bridge != surface->bridge ||
        seat->client != client || wl_resource_get_client(seat_resource) != client ||
        wl_resource_get_client(resource) != client ||
        seat->pressed_button != AKAOS_BTN_LEFT ||
        seat->last_pointer_press_serial != serial ||
        seat->focused_surface != surface->wl_surface ||
        surface->bridge->grabbed_popup ||
        surface->bridge->resizing_surface ||
        (surface->bridge->moving_surface &&
         (surface->bridge->moving_surface != surface ||
          surface->bridge->moving_seat != seat)))
        return;

    struct bridge *bridge = surface->bridge;
    bridge->moving_surface = surface;
    bridge->moving_seat = seat;
    bridge->move_offset_x = seat->last_pointer_press_x - surface->x;
    bridge->move_offset_y = seat->last_pointer_press_y - surface->y;
    int32_t x = bridge->pointer_x - bridge->move_offset_x;
    int32_t y = bridge->pointer_y - bridge->move_offset_y;
    if (surface->x != x || surface->y != y) {
        surface->x = x;
        surface->y = y;
        (void)akaos_toolkit_surface_move(&surface->compositor_surface, x, y);
    }
}

static void toplevel_resize(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat_resource,
                            uint32_t serial,
                            uint32_t edges) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    struct bridge_seat *seat = seat_resource
        ? wl_resource_get_user_data(seat_resource) : NULL;
    if (!xdg_toplevel_resize_edge_is_valid(edges, 1U) ||
        edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
        wl_resource_post_error(resource,
                               XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE,
                               "invalid interactive resize edge");
        return;
    }
    if (!surface || !surface->bridge || surface->toplevel != resource ||
        !surface->attached || !seat || seat->bridge != surface->bridge ||
        seat->client != client ||
        wl_resource_get_client(seat_resource) != client ||
        wl_resource_get_client(resource) != client ||
        seat->pressed_button != AKAOS_BTN_LEFT ||
        seat->last_pointer_press_serial != serial ||
        seat->focused_surface != surface->wl_surface ||
        surface->bridge->grabbed_popup ||
        surface->bridge->moving_surface ||
        (surface->bridge->resizing_surface &&
         (surface->bridge->resizing_surface != surface ||
          surface->bridge->resizing_seat != seat)))
        return;

    struct bridge *bridge = surface->bridge;
    bridge->resizing_surface = surface;
    bridge->resizing_seat = seat;
    bridge->resize_edges = edges;
    bridge->resize_start_pointer_x = seat->last_pointer_press_x;
    bridge->resize_start_pointer_y = seat->last_pointer_press_y;
    bridge->resize_start_x = surface->x;
    bridge->resize_start_y = surface->y;
    bridge->resize_start_width = surface->width;
    bridge->resize_start_height = surface->height;
    bridge->resize_desired_x = surface->x;
    bridge->resize_desired_y = surface->y;
    bridge->resize_desired_width = surface->width;
    bridge->resize_desired_height = surface->height;
    surface->resize_anchor_right = surface->x + (int32_t)surface->width;
    surface->resize_anchor_bottom = surface->y + (int32_t)surface->height;
    update_interactive_resize(bridge);
}

static void toplevel_set_min_size(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t width, int32_t height) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (width < 0 || height < 0 || width > (int32_t)WAYLAND_MAX_DIMENSION ||
        height > (int32_t)WAYLAND_MAX_DIMENSION) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                               "invalid minimum toplevel size");
        return;
    }
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    surface->min_width = width;
    surface->min_height = height;
}

static void toplevel_set_max_size(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t width, int32_t height) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (width < 0 || height < 0 || width > (int32_t)WAYLAND_MAX_DIMENSION ||
        height > (int32_t)WAYLAND_MAX_DIMENSION) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                               "invalid maximum toplevel size");
        return;
    }
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    surface->max_width = width;
    surface->max_height = height;
}

static int publish_toplevel_state(struct bridge_surface *surface,
                                  uint32_t additional_flags) {
    if (!surface || !surface->bridge || !surface->bridge->channel) return 0;
    if (!surface->attached) {
        if (additional_flags & AKAOS_SURFACE_FLAG_MINIMIZED)
            surface->minimized = 1;
        return 1;
    }
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_STATE;
    message.flags = AKAOS_SURFACE_FLAG_WAYLAND |
                    surface->window_state_flags |
                    (surface->minimized
                         ? AKAOS_SURFACE_FLAG_MINIMIZED : 0U) |
                    additional_flags;
    message.surface_id = surface->surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = surface->x;
    message.y = surface->y;
    if (!akaos_compositor_submit(surface->bridge->channel, &message))
        return 0;
    return 1;
}

static void toplevel_apply_window_state(struct bridge_surface *surface,
                                        uint32_t new_state_flags) {
    if (!surface || !surface->bridge || !surface->toplevel) return;
    struct bridge *bridge = surface->bridge;
    const uint32_t state_mask = AKAOS_SURFACE_FLAG_MAXIMIZED |
                                AKAOS_SURFACE_FLAG_FULLSCREEN;
    uint32_t old_state_flags = surface->window_state_flags & state_mask;
    new_state_flags &= state_mask;

    /* A state change supersedes a pointer-driven move/resize on this surface.
     * Do not let a late pointer release race make maximize/fullscreen vanish. */
    if (bridge->moving_surface == surface) {
        bridge->moving_surface = NULL;
        bridge->moving_seat = NULL;
    }
    if (bridge->resizing_surface == surface) {
        bridge->resizing_surface = NULL;
        bridge->resizing_seat = NULL;
        bridge->resize_edges = 0U;
    }

    if (surface->resize_configure_pending) {
        if (new_state_flags == old_state_flags) {
            surface->pending_window_state_valid = 0;
            return;
        }
        surface->pending_window_state_flags = new_state_flags;
        surface->pending_window_state_valid = 1;
        return;
    }
    if (old_state_flags == new_state_flags) return;

    if (old_state_flags == 0U && new_state_flags != 0U &&
        !surface->restore_geometry_valid) {
        surface->restore_x = surface->x;
        surface->restore_y = surface->y;
        surface->restore_width = surface->width ? surface->width : 640U;
        surface->restore_height = surface->height ? surface->height : 480U;
        surface->restore_geometry_valid = 1;
    }

    surface->window_state_flags = new_state_flags;
    foreign_toplevel_send_state(surface);
    int32_t x = surface->x;
    int32_t y = surface->y;
    uint32_t width = surface->width ? surface->width : 640U;
    uint32_t height = surface->height ? surface->height : 480U;
    toplevel_state_geometry(surface, new_state_flags, &x, &y, &width, &height);
    surface->x = x;
    surface->y = y;
    if (new_state_flags == 0U) surface->restore_geometry_valid = 0;

    if (surface->attached && !publish_toplevel_state(surface, 0U))
        fprintf(stderr, "wayland-bridge: window-state IPC queue full for %u\n",
                surface->surface_id);

    if (surface->configured) {
        bridge->resize_edges = 0U;
        if (!send_resize_configure(surface, x, y, width, height, 1))
            fprintf(stderr,
                    "wayland-bridge: window-state configure failed for %u\n",
                    surface->surface_id);
    }
}

static uint32_t toplevel_requested_window_state(
    const struct bridge_surface *surface) {
    if (!surface) return 0U;
    return surface->pending_window_state_valid
        ? surface->pending_window_state_flags : surface->window_state_flags;
}

static void toplevel_set_minimized_state(struct bridge_surface *surface,
                                         int minimized) {
    if (!surface) return;
    surface->minimized = minimized;
    if (!publish_toplevel_state(surface, 0U))
        fprintf(stderr, "wayland-bridge: minimize state IPC queue full for %u\n",
                surface->surface_id);
    foreign_toplevel_send_state(surface);
}

static void toplevel_set_maximized(struct wl_client *client,
                                   struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    toplevel_apply_window_state(
        surface, (toplevel_requested_window_state(surface) &
                  ~AKAOS_SURFACE_FLAG_FULLSCREEN) |
                 AKAOS_SURFACE_FLAG_MAXIMIZED);
}

static void toplevel_unset_maximized(struct wl_client *client,
                                     struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    toplevel_apply_window_state(surface,
        toplevel_requested_window_state(surface) &
            ~AKAOS_SURFACE_FLAG_MAXIMIZED);
}

static void toplevel_set_fullscreen(struct wl_client *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *output) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client ||
        (output && wl_resource_get_client(output) != client))
        return;
    toplevel_apply_window_state(
        surface, (toplevel_requested_window_state(surface) &
                  ~AKAOS_SURFACE_FLAG_MAXIMIZED) |
                 AKAOS_SURFACE_FLAG_FULLSCREEN);
}

static void toplevel_unset_fullscreen(struct wl_client *client,
                                      struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    toplevel_apply_window_state(surface,
        toplevel_requested_window_state(surface) &
            ~AKAOS_SURFACE_FLAG_FULLSCREEN);
}

static void toplevel_set_minimized(struct wl_client *client,
                                   struct wl_resource *resource) {
    struct bridge_surface *surface = wl_resource_get_user_data(resource);
    if (!surface || surface->toplevel != resource ||
        wl_resource_get_client(resource) != client)
        return;
    toplevel_set_minimized_state(surface, 1);
}

static const struct xdg_toplevel_interface xdg_toplevel_implementation = {
    .destroy = toplevel_destroy,
    .set_parent = toplevel_parent,
    .set_title = toplevel_title,
    .set_app_id = toplevel_app_id,
    .show_window_menu = toplevel_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_set_max_size,
    .set_min_size = toplevel_set_min_size,
    .set_maximized = toplevel_set_maximized,
    .unset_maximized = toplevel_unset_maximized,
    .set_fullscreen = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    .set_minimized = toplevel_set_minimized,
};

static void foreign_handle_unlink(struct bridge_foreign_handle *handle) {
    if (!handle) return;
    if (handle->manager) {
        struct bridge_foreign_handle **link = &handle->manager->handles;
        while (*link && *link != handle) link = &(*link)->next_manager;
        if (*link) *link = handle->next_manager;
    }
    if (handle->surface) {
        struct bridge_foreign_handle **link =
            &handle->surface->foreign_handles;
        while (*link && *link != handle) link = &(*link)->next_surface;
        if (*link) *link = handle->next_surface;
    }
    free(handle);
}

static void foreign_handle_destroyed(struct wl_resource *resource) {
    foreign_handle_unlink(wl_resource_get_user_data(resource));
}

static void foreign_manager_destroyed(struct wl_resource *resource) {
    struct bridge_foreign_manager *manager =
        wl_resource_get_user_data(resource);
    if (!manager) return;
    while (manager->handles) {
        struct bridge_foreign_handle *handle = manager->handles;
        wl_resource_destroy(handle->resource);
    }
    if (manager->bridge) {
        struct bridge_foreign_manager **link =
            &manager->bridge->foreign_managers;
        while (*link && *link != manager) link = &(*link)->next;
        if (*link) *link = manager->next;
    }
    free(manager);
}

static int foreign_surface_is_active(const struct bridge_surface *surface) {
    return surface_has_keyboard_focus(surface);
}

static void foreign_handle_send_state(struct bridge_foreign_handle *handle) {
    if (!handle || !handle->resource || !handle->surface) return;
    struct bridge_surface *surface = handle->surface;
    struct wl_array states;
    wl_array_init(&states);
    uint32_t value;
    if (surface->window_state_flags & AKAOS_SURFACE_FLAG_MAXIMIZED) {
        value = 0U;
        uint32_t *entry = wl_array_add(&states, sizeof(value));
        if (!entry) goto no_memory;
        *entry = value;
    }
    if (surface->minimized) {
        value = 1U;
        uint32_t *entry = wl_array_add(&states, sizeof(value));
        if (!entry) goto no_memory;
        *entry = value;
    }
    if (foreign_surface_is_active(surface)) {
        value = 2U;
        uint32_t *entry = wl_array_add(&states, sizeof(value));
        if (!entry) goto no_memory;
        *entry = value;
    }
    if ((surface->window_state_flags & AKAOS_SURFACE_FLAG_FULLSCREEN) &&
        wl_resource_get_version(handle->resource) >= 2) {
        value = 3U;
        uint32_t *entry = wl_array_add(&states, sizeof(value));
        if (!entry) goto no_memory;
        *entry = value;
    }
    zwlr_foreign_toplevel_handle_v1_send_state(handle->resource, &states);
    zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
    wl_array_release(&states);
    return;

no_memory:
    wl_array_release(&states);
    wl_client_post_no_memory(wl_resource_get_client(handle->resource));
}

static void foreign_toplevel_send_metadata(struct bridge_surface *surface) {
    if (!surface) return;
    for (struct bridge_foreign_handle *handle = surface->foreign_handles;
         handle; handle = handle->next_surface) {
        if (!handle->resource) continue;
        zwlr_foreign_toplevel_handle_v1_send_title(
            handle->resource, surface->title);
        zwlr_foreign_toplevel_handle_v1_send_app_id(
            handle->resource, surface->app_id);
        zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
    }
}

static void foreign_toplevel_send_state(struct bridge_surface *surface) {
    if (!surface) return;
    for (struct bridge_foreign_handle *handle = surface->foreign_handles;
         handle; handle = handle->next_surface)
        foreign_handle_send_state(handle);
}

static void foreign_toplevel_unmap(struct bridge_surface *surface) {
    if (!surface) return;
    while (surface->foreign_handles) {
        struct bridge_foreign_handle *handle = surface->foreign_handles;
        surface->foreign_handles = handle->next_surface;
        handle->next_surface = NULL;
        handle->surface = NULL;
        if (handle->resource)
            zwlr_foreign_toplevel_handle_v1_send_closed(handle->resource);
    }
}

static void foreign_toplevel_map_one(
    struct bridge_foreign_manager *manager, struct bridge_surface *surface) {
    if (!manager || manager->stopped || !manager->resource || !surface ||
        !surface->attached || !surface->toplevel)
        return;
    for (struct bridge_foreign_handle *existing = surface->foreign_handles;
         existing; existing = existing->next_surface) {
        if (existing->manager == manager) return;
    }
    struct wl_client *client = wl_resource_get_client(manager->resource);
    struct bridge_foreign_handle *handle = calloc(1U, sizeof(*handle));
    if (!handle) {
        wl_client_post_no_memory(client);
        return;
    }
    int version = wl_resource_get_version(manager->resource);
    handle->resource = wl_resource_create(
        client, &zwlr_foreign_toplevel_handle_v1_interface, version, 0U);
    if (!handle->resource) {
        free(handle);
        wl_client_post_no_memory(client);
        return;
    }
    handle->manager = manager;
    handle->surface = surface;
    handle->next_manager = manager->handles;
    manager->handles = handle;
    handle->next_surface = surface->foreign_handles;
    surface->foreign_handles = handle;
    wl_resource_set_implementation(handle->resource,
                                   &foreign_handle_implementation, handle,
                                   foreign_handle_destroyed);
    zwlr_foreign_toplevel_manager_v1_send_toplevel(manager->resource,
                                                   handle->resource);
    for (struct bridge_output *output = manager->bridge->outputs;
         output; output = output->next) {
        if (output->resource &&
            wl_resource_get_client(output->resource) == client)
            zwlr_foreign_toplevel_handle_v1_send_output_enter(
                handle->resource, output->resource);
    }
    zwlr_foreign_toplevel_handle_v1_send_title(
        handle->resource, surface->title[0] ? surface->title : "Wayland Window");
    zwlr_foreign_toplevel_handle_v1_send_app_id(handle->resource,
                                                surface->app_id);
    foreign_handle_send_state(handle);
    fprintf(stderr, "wayland-bridge: foreign-toplevel mapped id=%u\n",
            surface->surface_id);
}

static void foreign_toplevel_map(struct bridge_surface *surface) {
    if (!surface || !surface->bridge || !surface->attached ||
        !surface->toplevel)
        return;
    for (struct bridge_foreign_manager *manager =
             surface->bridge->foreign_managers;
         manager; manager = manager->next)
        foreign_toplevel_map_one(manager, surface);
}

static void foreign_manager_stop(struct wl_client *client,
                                 struct wl_resource *resource) {
    (void)client;
    struct bridge_foreign_manager *manager = wl_resource_get_user_data(resource);
    if (manager) manager->stopped = 1;
    zwlr_foreign_toplevel_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
foreign_manager_implementation = {
    .stop = foreign_manager_stop,
};

static void bind_foreign_manager(struct wl_client *client, void *data,
                                 uint32_t version, uint32_t id) {
    struct bridge *bridge = data;
    struct wl_resource *resource = wl_resource_create(
        client, &zwlr_foreign_toplevel_manager_v1_interface,
        version < 2U ? (int)version : 2, id);
    struct bridge_foreign_manager *manager = calloc(1U, sizeof(*manager));
    if (!resource || !manager) {
        if (resource) wl_resource_destroy(resource);
        free(manager);
        wl_client_post_no_memory(client);
        return;
    }
    manager->bridge = bridge;
    manager->resource = resource;
    manager->next = bridge->foreign_managers;
    bridge->foreign_managers = manager;
    wl_resource_set_implementation(resource, &foreign_manager_implementation,
                                   manager, foreign_manager_destroyed);
    fprintf(stderr, "wayland-bridge: foreign-toplevel manager bound\n");
    for (size_t index = 0; index < WAYLAND_SURFACE_LIMIT; ++index)
        foreign_toplevel_map_one(manager, bridge->surfaces[index]);
}

static void foreign_handle_set_maximized(struct wl_client *client,
                                         struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (!handle || !handle->surface || !handle->surface->toplevel) return;
    struct bridge_surface *surface = handle->surface;
    toplevel_apply_window_state(
        surface, (toplevel_requested_window_state(surface) &
                  ~AKAOS_SURFACE_FLAG_FULLSCREEN) |
                 AKAOS_SURFACE_FLAG_MAXIMIZED);
    (void)client;
}

static void foreign_handle_unset_maximized(struct wl_client *client,
                                           struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (!handle || !handle->surface || !handle->surface->toplevel) return;
    struct bridge_surface *surface = handle->surface;
    toplevel_apply_window_state(surface,
        toplevel_requested_window_state(surface) &
            ~AKAOS_SURFACE_FLAG_MAXIMIZED);
    (void)client;
}

static void foreign_handle_set_minimized(struct wl_client *client,
                                         struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (handle && handle->surface) toplevel_set_minimized_state(handle->surface, 1);
    (void)client;
}

static void foreign_handle_unset_minimized(struct wl_client *client,
                                           struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (handle && handle->surface) toplevel_set_minimized_state(handle->surface, 0);
    (void)client;
}

static void foreign_handle_activate(struct wl_client *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *seat_resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    struct bridge_seat *seat = seat_resource
        ? wl_resource_get_user_data(seat_resource) : NULL;
    if (!handle || !handle->surface || !handle->surface->attached || !seat ||
        seat->bridge != handle->surface->bridge || seat->client != client ||
        wl_resource_get_client(seat_resource) != client)
        return;
    struct bridge_surface *surface = handle->surface;
    if (!akaos_toolkit_surface_focus(&surface->compositor_surface)) {
        fprintf(stderr, "wayland-bridge: activation IPC queue full for %u\n",
                surface->surface_id);
        return;
    }
    struct wl_client *target_client = wl_resource_get_client(surface->wl_surface);
    for (size_t index = 0; index < WAYLAND_SEAT_LIMIT; ++index) {
        struct bridge_seat *target_seat = &surface->bridge->seats[index];
        if (!target_seat->keyboard) continue;
        if (target_seat->client == target_client)
            send_keyboard_focus(surface->bridge, target_seat, surface);
        else if (target_seat->keyboard_focus_surface)
            send_keyboard_focus(surface->bridge, target_seat, NULL);
    }
}

static void foreign_handle_close(struct wl_client *client,
                                 struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (handle && handle->surface && handle->surface->toplevel) {
        fprintf(stderr, "wayland-bridge: foreign-toplevel close requested id=%u\n",
                handle->surface->surface_id);
        xdg_toplevel_send_close(handle->surface->toplevel);
    }
    (void)client;
}

static void foreign_handle_set_rectangle(
    struct wl_client *client, struct wl_resource *resource,
    struct wl_resource *surface_resource, int32_t x, int32_t y,
    int32_t width, int32_t height) {
    (void)client;
    (void)x;
    (void)y;
    if (width < 0 || height < 0 || !surface_resource ||
        wl_resource_get_client(surface_resource) !=
            wl_resource_get_client(resource))
        wl_resource_post_error(
            resource,
            ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_ERROR_INVALID_RECTANGLE,
            "invalid foreign toplevel rectangle");
}

static void foreign_handle_destroy(struct wl_client *client,
                                   struct wl_resource *resource) {
    resource_destroy(client, resource);
}

static void foreign_handle_set_fullscreen(struct wl_client *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *output) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (!handle || !handle->surface || !handle->surface->toplevel ||
        (output && wl_resource_get_client(output) != client))
        return;
    struct bridge_surface *surface = handle->surface;
    toplevel_apply_window_state(
        surface, (toplevel_requested_window_state(surface) &
                  ~AKAOS_SURFACE_FLAG_MAXIMIZED) |
                 AKAOS_SURFACE_FLAG_FULLSCREEN);
}

static void foreign_handle_unset_fullscreen(struct wl_client *client,
                                            struct wl_resource *resource) {
    struct bridge_foreign_handle *handle = wl_resource_get_user_data(resource);
    if (!handle || !handle->surface || !handle->surface->toplevel) return;
    struct bridge_surface *surface = handle->surface;
    toplevel_apply_window_state(surface,
        toplevel_requested_window_state(surface) &
            ~AKAOS_SURFACE_FLAG_FULLSCREEN);
    (void)client;
}

static const struct zwlr_foreign_toplevel_handle_v1_interface
foreign_handle_implementation = {
    .set_maximized = foreign_handle_set_maximized,
    .unset_maximized = foreign_handle_unset_maximized,
    .set_minimized = foreign_handle_set_minimized,
    .unset_minimized = foreign_handle_unset_minimized,
    .activate = foreign_handle_activate,
    .close = foreign_handle_close,
    .set_rectangle = foreign_handle_set_rectangle,
    .destroy = foreign_handle_destroy,
    .set_fullscreen = foreign_handle_set_fullscreen,
    .unset_fullscreen = foreign_handle_unset_fullscreen,
};

static int register_globals(struct bridge *bridge) {
    return wl_global_create(bridge->display, &wl_compositor_interface, 4,
                            bridge, bind_compositor) != NULL &&
           wl_global_create(bridge->display, &wl_output_interface, 2,
                            bridge, bind_output_with_release) != NULL &&
           wl_global_create(bridge->display,
                           &wl_data_device_manager_interface, 1,
                           bridge, bind_data_manager) != NULL &&
           wl_global_create(bridge->display, &wl_seat_interface, 5,
                            bridge, bind_seat) != NULL &&
           wl_global_create(bridge->display, &wl_subcompositor_interface, 1,
                            bridge, bind_subcompositor) != NULL &&
           wl_global_create(bridge->display, &xdg_wm_base_interface, 1,
                            bridge, bind_wm_base) != NULL &&
           wl_global_create(bridge->display,
                            &zwlr_layer_shell_v1_interface, 4,
                            bridge, bind_layer_shell) != NULL &&
           wl_global_create(bridge->display,
                            &zwlr_foreign_toplevel_manager_v1_interface, 2,
                            bridge, bind_foreign_manager) != NULL;
}

int main(void) {
    static char *bridge_environment[] = {
        "HOME=/root",
        "PATH=/bin:/usr/bin",
        "USER=root",
        "LOGNAME=root",
        "SHELL=/bin/akash",
        "TERM=linux",
        "XDG_RUNTIME_DIR=/run/user/0",
        "WAYLAND_DISPLAY=wayland-0",
        NULL,
    };
    struct bridge bridge = {0};
    bridge.keymap_fd = -1;
    environ = bridge_environment;
    if (mkdir("/run/user/0", 0700) != 0 && errno != EEXIST) {
        printf("wayland-bridge: cannot create runtime directory (errno=%d)\n",
               errno);
        return 1;
    }
    if (!connect_compositor(&bridge)) {
        puts("wayland-bridge: Lumen compositor channel is unavailable");
        return 2;
    }
    if (!create_keyboard_keymap(&bridge)) {
        puts("wayland-bridge: cannot compile the evdev/us XKB keymap");
        release_keyboard_keymap(&bridge);
        (void)akaos_toolkit_disconnect(bridge.channel);
        return 3;
    }
    bridge.display = wl_display_create();
    if (!bridge.display || wl_display_init_shm(bridge.display) != 0 ||
        !register_globals(&bridge)) {
        puts("wayland-bridge: Wayland server initialization failed");
        if (bridge.display) wl_display_destroy(bridge.display);
        release_keyboard_keymap(&bridge);
        (void)akaos_toolkit_disconnect(bridge.channel);
        return 4;
    }
    const char *socket_name = wl_display_add_socket_auto(bridge.display);
    if (!socket_name) {
        printf("wayland-bridge: cannot create socket (errno=%d)\n", errno);
        wl_display_destroy(bridge.display);
        release_keyboard_keymap(&bridge);
        (void)akaos_toolkit_disconnect(bridge.channel);
        return 5;
    }
    struct wl_event_loop *event_loop =
        wl_display_get_event_loop(bridge.display);
    bridge.input_timer = wl_event_loop_add_timer(
        event_loop, dispatch_wayland_input, &bridge);
    if (!bridge.input_timer ||
        wl_event_source_timer_update(bridge.input_timer, 8U) != 0) {
        puts("wayland-bridge: cannot start input polling timer");
        wl_display_destroy(bridge.display);
        release_keyboard_keymap(&bridge);
        (void)akaos_toolkit_disconnect(bridge.channel);
        return 6;
    }
    printf("wayland-bridge READY %s %ux%u\n", socket_name,
           bridge.display_info.width, bridge.display_info.height);
    fflush(stdout);
    wl_display_run(bridge.display);
    wl_display_destroy_clients(bridge.display);
    wl_display_destroy(bridge.display);
    release_keyboard_keymap(&bridge);
    (void)akaos_toolkit_disconnect(bridge.channel);
    return 0;
}
