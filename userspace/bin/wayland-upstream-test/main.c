#include <akaos/display.h>
#include <akaos/input.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include "xdg-shell-client-protocol.h"
#include "wlr-foreign-toplevel-management-client-protocol.h"

#define TEST_CONFIGURE_MAXIMIZED UINT32_C(0x01)
#define TEST_CONFIGURE_FULLSCREEN UINT32_C(0x02)
#define TEST_CONFIGURE_ACTIVATED UINT32_C(0x04)

struct external_xdg_configure_state {
    volatile uint32_t configured;
    volatile uint32_t configure_generation;
    uint32_t configure_serial;
    int32_t width;
    int32_t height;
    uint32_t toplevel_states;
    volatile uint32_t close_received;
};

struct foreign_toplevel_test_state {
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    int saw_title;
    int saw_app_id;
    int saw_done;
    int saw_closed;
    int maximized;
    int minimized;
    int activated;
    int fullscreen;
};

struct wayland_smoke_state {
    struct wl_display *server;
    struct wl_display *client_display;
    volatile uint32_t compositor_bound;
    volatile uint32_t saw_compositor;
    volatile uint32_t saw_shm;
    volatile uint32_t clients_created;
    volatile uint32_t seat_capabilities;
    volatile uint32_t move_request_sent;
    volatile uint32_t resize_request_count;
    uint32_t need_pointer;
    uint32_t need_foreign;
    struct foreign_toplevel_test_state *foreign_test;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_subcompositor *subcompositor;
    struct xdg_wm_base *wm_base;
    struct zwlr_foreign_toplevel_manager_v1 *foreign_manager;
    struct xdg_toplevel *move_toplevel;
};

static void external_seat_capabilities(void *data, struct wl_seat *seat,
                                       uint32_t capabilities) {
    struct wayland_smoke_state *state = data;
    (void)seat;
    __atomic_store_n(&state->seat_capabilities, capabilities,
                     __ATOMIC_RELEASE);
}

static void external_seat_name(void *data, struct wl_seat *seat,
                               const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener external_seat_listener = {
    .capabilities = external_seat_capabilities,
    .name = external_seat_name,
};

static void external_pointer_enter(void *data, struct wl_pointer *pointer,
                                   uint32_t serial, struct wl_surface *surface,
                                   wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)pointer; (void)serial; (void)surface; (void)x; (void)y;
}

static void external_pointer_leave(void *data, struct wl_pointer *pointer,
                                   uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void external_pointer_motion(void *data, struct wl_pointer *pointer,
                                    uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)pointer; (void)time; (void)x; (void)y;
}

static void external_pointer_button(void *data, struct wl_pointer *pointer,
                                    uint32_t serial, uint32_t time,
                                    uint32_t button, uint32_t state_value) {
    struct wayland_smoke_state *state = data;
    (void)pointer;
    (void)time;
    if (button != AKAOS_BTN_LEFT ||
        state_value != WL_POINTER_BUTTON_STATE_PRESSED ||
        !state->move_toplevel || !state->seat)
        return;
    if (!__atomic_load_n(&state->move_request_sent, __ATOMIC_ACQUIRE)) {
        xdg_toplevel_move(state->move_toplevel, state->seat, serial);
        if (wl_display_flush(state->client_display) < 0) return;
        __atomic_store_n(&state->move_request_sent, 1U, __ATOMIC_RELEASE);
        puts("wayland-xdg-toplevel-move-request-sent");
        fflush(stdout);
        return;
    }
    uint32_t resize_count = __atomic_load_n(&state->resize_request_count,
                                            __ATOMIC_ACQUIRE);
    if (resize_count >= 2U) return;
    xdg_toplevel_resize(state->move_toplevel, state->seat, serial,
                        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
    if (wl_display_flush(state->client_display) < 0) return;
    __atomic_store_n(&state->resize_request_count, resize_count + 1U,
                     __ATOMIC_RELEASE);
    printf("wayland-xdg-toplevel-resize-request-%u-sent\n", resize_count + 1U);
    fflush(stdout);
}

static void external_pointer_axis(void *data, struct wl_pointer *pointer,
                                  uint32_t time, uint32_t axis,
                                  wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener external_pointer_listener = {
    .enter = external_pointer_enter,
    .leave = external_pointer_leave,
    .motion = external_pointer_motion,
    .button = external_pointer_button,
    .axis = external_pointer_axis,
};

static void external_keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                                     uint32_t format, int32_t fd,
                                     uint32_t size) {
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0) close(fd);
}

static void external_keyboard_enter(void *data, struct wl_keyboard *keyboard,
                                   uint32_t serial, struct wl_surface *surface,
                                   struct wl_array *keys) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void external_keyboard_leave(void *data, struct wl_keyboard *keyboard,
                                    uint32_t serial, struct wl_surface *surface) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void external_keyboard_key(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, uint32_t time,
                                  uint32_t key, uint32_t state) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)time;
    (void)key;
    (void)state;
}

static void external_keyboard_modifiers(
    void *data, struct wl_keyboard *keyboard, uint32_t serial,
    uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)depressed;
    (void)latched;
    (void)locked;
    (void)group;
}

static void external_keyboard_repeat_info(void *data,
                                         struct wl_keyboard *keyboard,
                                         int32_t rate, int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener external_keyboard_listener = {
    .keymap = external_keyboard_keymap,
    .enter = external_keyboard_enter,
    .leave = external_keyboard_leave,
    .key = external_keyboard_key,
    .modifiers = external_keyboard_modifiers,
    .repeat_info = external_keyboard_repeat_info,
};

static void foreign_manager_toplevel(
    void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
    struct zwlr_foreign_toplevel_handle_v1 *handle);
static void foreign_manager_finished(
    void *data, struct zwlr_foreign_toplevel_manager_v1 *manager);
static const struct zwlr_foreign_toplevel_manager_v1_listener
foreign_manager_listener = {
    .toplevel = foreign_manager_toplevel,
    .finished = foreign_manager_finished,
};

static void foreign_handle_title(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
    const char *title) {
    struct foreign_toplevel_test_state *state = data;
    if (strcmp(title, "akaOS Foreign Toplevel Test") == 0) {
        state->handle = handle;
        state->saw_title = 1;
    }
}

static void foreign_handle_app_id(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
    const char *app_id) {
    struct foreign_toplevel_test_state *state = data;
    if (strcmp(app_id, "akaos-foreign-toplevel-test") == 0) {
        state->handle = handle;
        state->saw_app_id = 1;
    }
}

static void foreign_handle_output_enter(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
    struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void foreign_handle_output_leave(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
    struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void foreign_handle_state(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
    struct wl_array *states) {
    struct foreign_toplevel_test_state *state = data;
    if (handle != state->handle) return;
    state->maximized = 0;
    state->minimized = 0;
    state->activated = 0;
    state->fullscreen = 0;
    uint32_t *value;
    wl_array_for_each(value, states) {
        if (*value == 0U) state->maximized = 1;
        else if (*value == 1U) state->minimized = 1;
        else if (*value == 2U) state->activated = 1;
        else if (*value == 3U) state->fullscreen = 1;
    }
}

static void foreign_handle_done(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct foreign_toplevel_test_state *state = data;
    if (handle != state->handle) return;
    state->saw_done = 1;
}

static void foreign_handle_closed(
    void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct foreign_toplevel_test_state *state = data;
    if (handle != state->handle) return;
    state->saw_closed = 1;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener
foreign_handle_listener = {
    .title = foreign_handle_title,
    .app_id = foreign_handle_app_id,
    .output_enter = foreign_handle_output_enter,
    .output_leave = foreign_handle_output_leave,
    .state = foreign_handle_state,
    .done = foreign_handle_done,
    .closed = foreign_handle_closed,
};

static void foreign_manager_toplevel(
    void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
    struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct foreign_toplevel_test_state *state = data;
    (void)manager;
    if (zwlr_foreign_toplevel_handle_v1_add_listener(
            handle, &foreign_handle_listener, state) != 0)
        zwlr_foreign_toplevel_handle_v1_destroy(handle);
}

static void foreign_manager_finished(
    void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {
    (void)data;
    (void)manager;
}

static const struct wl_compositor_interface compositor_implementation = {0};

static void bind_compositor(struct wl_client *client,
                            void *data,
                            uint32_t version,
                            uint32_t id) {
    struct wayland_smoke_state *state = data;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_compositor_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_implementation,
                                   state, NULL);
    __atomic_add_fetch(&state->compositor_bound, 1U, __ATOMIC_RELEASE);
}

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version) {
    struct wayland_smoke_state *state = data;
    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface,
            version < 4U ? version : 4U);
        if (state->compositor)
            __atomic_store_n(&state->saw_compositor, 1U, __ATOMIC_RELEASE);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                      version < 1U ? version : 1U);
        if (state->shm)
            __atomic_store_n(&state->saw_shm, 1U, __ATOMIC_RELEASE);
    } else if (strcmp(interface, "wl_seat") == 0 && state->need_pointer) {
        struct wl_seat *seat = wl_registry_bind(
            registry, name, &wl_seat_interface, 1U);
        if (seat && wl_seat_add_listener(seat, &external_seat_listener,
                                         state) == 0) {
            state->seat = seat;
        } else if (seat) {
            wl_seat_destroy(seat);
        }
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        state->subcompositor = wl_registry_bind(
            registry, name, &wl_subcompositor_interface,
            version < 1U ? version : 1U);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(registry, name,
                                          &xdg_wm_base_interface,
                                          version < 1U ? version : 1U);
    } else if (strcmp(interface,
                      "zwlr_foreign_toplevel_manager_v1") == 0 &&
               state->need_foreign && state->foreign_test) {
        state->foreign_manager = wl_registry_bind(
            registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
            version < 2U ? version : 2U);
        state->foreign_test->manager = state->foreign_manager;
        if (state->foreign_manager &&
            zwlr_foreign_toplevel_manager_v1_add_listener(
                state->foreign_manager, &foreign_manager_listener,
                state->foreign_test) != 0) {
            zwlr_foreign_toplevel_manager_v1_destroy(state->foreign_manager);
            state->foreign_manager = NULL;
            state->foreign_test->manager = NULL;
        }
    }
}

static void registry_global_remove(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                            uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener external_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial) {
    struct external_xdg_configure_state *state = data;
    xdg_surface_ack_configure(surface, serial);
    state->configure_serial = serial;
    __atomic_store_n(&state->configured, 1U, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->configure_generation, 1U, __ATOMIC_RELEASE);
}

static const struct xdg_surface_listener external_xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
    struct external_xdg_configure_state *state = data;
    (void)toplevel;
    state->toplevel_states = 0U;
    uint32_t *state_value;
    wl_array_for_each(state_value, states) {
        if (*state_value == XDG_TOPLEVEL_STATE_MAXIMIZED)
            state->toplevel_states |= TEST_CONFIGURE_MAXIMIZED;
        else if (*state_value == XDG_TOPLEVEL_STATE_FULLSCREEN)
            state->toplevel_states |= TEST_CONFIGURE_FULLSCREEN;
        else if (*state_value == XDG_TOPLEVEL_STATE_ACTIVATED)
            state->toplevel_states |= TEST_CONFIGURE_ACTIVATED;
    }
    state->width = width;
    state->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct external_xdg_configure_state *state = data;
    (void)toplevel;
    if (state) {
        __atomic_store_n(&state->close_received, 1U, __ATOMIC_RELEASE);
        puts("wayland-xdg-toplevel-close-event-received");
        fflush(stdout);
    }
}

static void xdg_toplevel_configure_bounds(void *data,
                                         struct xdg_toplevel *toplevel,
                                         int32_t width, int32_t height) {
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void xdg_toplevel_wm_capabilities(void *data,
                                         struct xdg_toplevel *toplevel,
                                         struct wl_array *capabilities) {
    (void)data;
    (void)toplevel;
    (void)capabilities;
}

static const struct xdg_toplevel_listener external_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

static struct wayland_smoke_state *listener_state;

static void client_created_notify(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
    if (listener_state)
        __atomic_add_fetch(&listener_state->clients_created, 1U,
                           __ATOMIC_RELEASE);
}

static void *run_server(void *opaque) {
    struct wayland_smoke_state *state = opaque;
    wl_display_run(state->server);
    return NULL;
}

static struct wl_buffer *create_external_test_buffer(
    struct wayland_smoke_state *state, uint32_t width, uint32_t height,
    uint32_t color) {
    if (!state || !state->shm || width == 0U || height == 0U ||
        width > UINT32_MAX / 4U || height > UINT32_MAX / (width * 4U))
        return NULL;
    uint32_t stride = width * 4U;
    size_t size = (size_t)stride * height;
    int fd = memfd_create("akaos-subsurface-test", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
        if (fd >= 0) close(fd);
        return NULL;
    }
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                            fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    for (size_t index = 0; index < (size_t)width * height; ++index)
        pixels[index] = color;
    (void)munmap(pixels, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd,
                                                  (int32_t)size);
    close(fd);
    if (!pool) return NULL;
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
        pool, 0, (int32_t)width, (int32_t)height, (int32_t)stride,
        WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buffer;
}

struct subsurface_slot_probe {
    uint32_t surface_id;
    uint32_t client_pid;
    uint32_t parent_surface_id;
    uint32_t z_order;
    uint32_t flags;
    uint32_t configure_serial;
    uint32_t configure_ack;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

static int inspect_subsurface_slots(
    struct akaos_compositor_channel *channel,
    const uint32_t colors[4], struct subsurface_slot_probe probes[4]) {
    if (!channel || !colors || !probes) return 0;
    uint32_t found = 0U;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[index];
        if (!__atomic_load_n(&slot->active, __ATOMIC_ACQUIRE) ||
            !(slot->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
            !slot->shm_name[0])
            continue;
        int fd = shm_open(slot->shm_name, O_RDWR, 0);
        if (fd < 0) continue;
        struct stat status;
        if (fstat(fd, &status) != 0 || status.st_size <
                (off_t)sizeof(struct akaos_surface_header)) {
            close(fd);
            continue;
        }
        size_t mapping_size = (size_t)status.st_size;
        void *mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
        close(fd);
        if (mapping == MAP_FAILED) continue;
        const struct akaos_surface_header *header = mapping;
        if (header->width == 0U || header->height == 0U ||
            header->pixel_offset > mapping_size ||
            mapping_size - header->pixel_offset < sizeof(uint32_t)) {
            (void)munmap(mapping, mapping_size);
            continue;
        }
        uint32_t color = *(const uint32_t *)((const uint8_t *)mapping +
                                             header->pixel_offset);
        for (size_t color_index = 0; color_index < 4U; ++color_index) {
            uint32_t bit = UINT32_C(1) << color_index;
            if (color == colors[color_index] && !(found & bit)) {
                probes[color_index].surface_id = slot->surface_id;
                probes[color_index].client_pid = slot->client_pid;
                probes[color_index].parent_surface_id =
                    slot->parent_surface_id;
                probes[color_index].z_order = slot->z_order;
                probes[color_index].flags = slot->flags;
                probes[color_index].configure_serial = slot->configure_serial;
                probes[color_index].configure_ack = slot->configure_ack;
                probes[color_index].x = slot->x;
                probes[color_index].y = slot->y;
                probes[color_index].width = slot->width;
                probes[color_index].height = slot->height;
                found |= bit;
                break;
            }
        }
        (void)munmap(mapping, mapping_size);
    }
    return found == UINT32_C(15);
}

static int commit_resize_configure(
    struct wayland_smoke_state *state,
    struct external_xdg_configure_state *configure_state,
    struct wl_display *display, struct wl_surface *parent,
    struct akaos_compositor_channel *channel, const uint32_t colors[4],
    struct subsurface_slot_probe probes[4], uint32_t previous_generation,
    uint32_t expected_width, uint32_t expected_height,
    uint32_t required_toplevel_states,
    uint32_t required_slot_flags, uint32_t forbidden_slot_flags,
    struct wl_buffer **buffer_out) {
    if (!state || !configure_state || !display || !parent ||
        !channel || !colors || !probes || !buffer_out)
        return 0;
    *buffer_out = NULL;
    int configured = 0;
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (__atomic_load_n(&configure_state->configure_generation,
                            __ATOMIC_ACQUIRE) > previous_generation) {
            configured = 1;
            break;
        }
        if (wl_display_dispatch(display) < 0) return 0;
    }
    if (!configured || configure_state->width != (int32_t)expected_width ||
        configure_state->height != (int32_t)expected_height ||
        configure_state->configure_serial == 0U ||
        (configure_state->toplevel_states & required_toplevel_states) !=
            required_toplevel_states)
        return 0;

    struct wl_buffer *buffer = create_external_test_buffer(
        state, expected_width, expected_height, colors[0]);
    if (!buffer) return 0;
    *buffer_out = buffer;
    wl_surface_attach(parent, buffer, 0, 0);
    wl_surface_damage(parent, 0, 0, (int32_t)expected_width,
                      (int32_t)expected_height);
    wl_surface_commit(parent);
    if (wl_display_roundtrip(display) < 0) return 0;

    for (unsigned attempt = 0U; attempt < 400U; ++attempt) {
        if (inspect_subsurface_slots(channel, colors, probes) &&
            probes[0].width == expected_width &&
            probes[0].height == expected_height &&
            probes[0].configure_serial != 0U &&
            probes[0].configure_ack == probes[0].configure_serial &&
            (probes[0].flags & required_slot_flags) == required_slot_flags &&
            (probes[0].flags & forbidden_slot_flags) == 0U &&
            probes[1].x == probes[0].x + 18 &&
            probes[1].y == probes[0].y + 18 &&
            probes[2].x == probes[0].x + 18 &&
            probes[2].y == probes[0].y + 18 &&
            probes[3].x == probes[0].x + 24 &&
            probes[3].y == probes[0].y + 24)
            return 1;
        (void)usleep(10000U);
    }
    return 0;
}

static int run_external_subsurface_smoke(void) {
    struct wayland_smoke_state state = {0};
    state.need_pointer = 1U;
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;
    struct wl_surface *parent = NULL;
    struct wl_surface *child = NULL;
    struct wl_surface *upper_child = NULL;
    struct wl_surface *nested_child = NULL;
    struct xdg_surface *xdg_parent = NULL;
    struct xdg_toplevel *toplevel = NULL;
    struct wl_subsurface *subsurface = NULL;
    struct wl_subsurface *upper_subsurface = NULL;
    struct wl_subsurface *nested_subsurface = NULL;
    struct wl_buffer *parent_buffer = NULL;
    struct wl_buffer *child_buffer = NULL;
    struct wl_buffer *upper_buffer = NULL;
    struct wl_buffer *nested_buffer = NULL;
    struct wl_buffer *resize_buffers[2] = {NULL, NULL};
    struct wl_buffer *state_buffers[3] = {NULL, NULL, NULL};
    struct subsurface_slot_probe probes[4] = {0};
    struct external_xdg_configure_state configure_state = {0};
    const uint32_t colors[4] = {
        UINT32_C(0xff354f75), UINT32_C(0xff15bf78), UINT32_C(0xffed5123),
        UINT32_C(0xffe6c547),
    };
    struct akaos_compositor_channel *channel = MAP_FAILED;
    int channel_fd = -1;
    int result = 1;
    const char *failure_stage = "environment";

    if (setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0 ||
        setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0)
        goto cleanup;
    failure_stage = "Wayland connect";
    display = wl_display_connect(NULL);
    if (!display) goto cleanup;
    failure_stage = "registry creation";
    registry = wl_display_get_registry(display);
    if (!registry || wl_registry_add_listener(registry, &registry_listener,
                                               &state) != 0)
        goto cleanup;
    failure_stage = "registry roundtrip";
    if (wl_display_roundtrip(display) < 0) goto cleanup;
    failure_stage = "global bind event roundtrip";
    if (wl_display_roundtrip(display) < 0) goto cleanup;
    failure_stage = "required globals and seat capabilities";
    if (!state.compositor || !state.shm || !state.seat ||
        !(__atomic_load_n(&state.seat_capabilities, __ATOMIC_ACQUIRE) &
          WL_SEAT_CAPABILITY_POINTER) || !state.subcompositor || !state.wm_base)
        goto cleanup;
    puts("wayland-xdg-move-seat-ready");
    fflush(stdout);
    state.client_display = display;
    failure_stage = "wl_pointer creation";
    state.pointer = wl_seat_get_pointer(state.seat);
    if (!state.pointer ||
        wl_pointer_add_listener(state.pointer, &external_pointer_listener,
                                &state) != 0)
        goto cleanup;
    puts("wayland-xdg-move-pointer-ready");
    fflush(stdout);
    if (xdg_wm_base_add_listener(state.wm_base,
                                 &external_wm_base_listener, NULL) != 0)
        goto cleanup;

    failure_stage = "surface creation";
    parent = wl_compositor_create_surface(state.compositor);
    child = wl_compositor_create_surface(state.compositor);
    upper_child = wl_compositor_create_surface(state.compositor);
    nested_child = wl_compositor_create_surface(state.compositor);
    if (!parent || !child || !upper_child || !nested_child) goto cleanup;
    xdg_parent = xdg_wm_base_get_xdg_surface(state.wm_base, parent);
    failure_stage = "xdg_surface listener";
    if (!xdg_parent ||
        xdg_surface_add_listener(xdg_parent,
                                 &external_xdg_surface_listener,
                                 &configure_state) != 0)
        goto cleanup;
    failure_stage = "xdg_toplevel creation";
    toplevel = xdg_surface_get_toplevel(xdg_parent);
    if (!toplevel ||
        xdg_toplevel_add_listener(toplevel, &external_toplevel_listener,
                                  &configure_state) != 0)
        goto cleanup;
    state.move_toplevel = toplevel;
    xdg_toplevel_set_title(toplevel, "akaOS Subsurface Test");
    xdg_toplevel_set_min_size(toplevel, 600, 400);
    xdg_toplevel_set_max_size(toplevel, 670, 600);
    failure_stage = "initial xdg configure";
    wl_surface_commit(parent);
    if (wl_display_roundtrip(display) < 0 ||
        !__atomic_load_n(&configure_state.configured, __ATOMIC_ACQUIRE) ||
        configure_state.width <= 0 || configure_state.height <= 0)
        goto cleanup;
    puts("wayland-xdg-move-toplevel-configured");
    fflush(stdout);

    failure_stage = "subsurface role creation";
    subsurface = wl_subcompositor_get_subsurface(state.subcompositor, child,
                                                  parent);
    upper_subsurface = wl_subcompositor_get_subsurface(
        state.subcompositor, upper_child, parent);
    nested_subsurface = wl_subcompositor_get_subsurface(
        state.subcompositor, nested_child, child);
    if (!subsurface || !upper_subsurface || !nested_subsurface) goto cleanup;
    wl_subsurface_set_position(subsurface, 18, 18);
    wl_subsurface_set_position(upper_subsurface, 18, 18);
    wl_subsurface_set_position(nested_subsurface, 6, 6);
    wl_subsurface_place_below(subsurface, parent);
    wl_subsurface_place_above(upper_subsurface, parent);
    wl_subsurface_place_above(nested_subsurface, child);
    failure_stage = "SHM buffer creation";
    child_buffer = create_external_test_buffer(&state, 24U, 24U,
                                                colors[1]);
    upper_buffer = create_external_test_buffer(&state, 24U, 24U, colors[2]);
    nested_buffer = create_external_test_buffer(&state, 12U, 12U, colors[3]);
    parent_buffer = create_external_test_buffer(
        &state, (uint32_t)configure_state.width,
        (uint32_t)configure_state.height, colors[0]);
    if (!child_buffer || !upper_buffer || !nested_buffer || !parent_buffer)
        goto cleanup;
    puts("wayland-xdg-move-buffers-ready");
    fflush(stdout);

    failure_stage = "synchronized child commits";
    wl_surface_attach(child, child_buffer, 0, 0);
    wl_surface_damage(child, 0, 0, 24, 24);
    wl_surface_commit(child);
    wl_surface_attach(upper_child, upper_buffer, 0, 0);
    wl_surface_damage(upper_child, 0, 0, 24, 24);
    wl_surface_commit(upper_child);
    wl_surface_attach(nested_child, nested_buffer, 0, 0);
    wl_surface_damage(nested_child, 0, 0, 12, 12);
    wl_surface_commit(nested_child);
    if (wl_display_roundtrip(display) < 0) goto cleanup;
    puts("wayland-subsurface-child-commits-staged");
    fflush(stdout);
    (void)usleep(800000U);

    failure_stage = "parent buffer commit";
    wl_surface_attach(parent, parent_buffer, 0, 0);
    wl_surface_damage(parent, 0, 0, configure_state.width,
                      configure_state.height);
    wl_surface_commit(parent);
    if (wl_display_roundtrip(display) < 0) goto cleanup;
    puts("wayland-subsurface-parent-commit-complete");
    fflush(stdout);

    failure_stage = "compositor IPC mapping";
    channel_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0);
    if (channel_fd < 0) goto cleanup;
    channel = mmap(NULL, sizeof(*channel), PROT_READ | PROT_WRITE, MAP_SHARED,
                   channel_fd, 0);
    if (channel == MAP_FAILED) {
        channel = NULL;
        goto cleanup;
    }
    int stack_verified = 0;
    failure_stage = "subsurface slot verification";
    for (unsigned attempt = 0U; attempt < 400U; ++attempt) {
        if (inspect_subsurface_slots(channel, colors, probes) &&
            probes[0].parent_surface_id == 0U &&
            probes[1].parent_surface_id == probes[0].surface_id &&
            probes[2].parent_surface_id == probes[0].surface_id &&
            probes[3].parent_surface_id == probes[1].surface_id &&
            (probes[1].flags & AKAOS_SURFACE_FLAG_SUBSURFACE) &&
            !(probes[1].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) &&
            (probes[2].flags & AKAOS_SURFACE_FLAG_SUBSURFACE) &&
            (probes[2].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) &&
            (probes[3].flags & AKAOS_SURFACE_FLAG_SUBSURFACE) &&
            (probes[3].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE)) {
            stack_verified = 1;
            break;
        }
        (void)usleep(10000U);
    }
    if (!stack_verified) goto cleanup;
    puts("wayland-subsurface-nested-tree-verified");
    fflush(stdout);
    puts("wayland-subsurface-sibling-z-order-verified");
    fflush(stdout);

    int32_t original_x = probes[0].x;
    int32_t original_y = probes[0].y;
    failure_stage = "Lumen titlebar drag propagation";
    puts("wayland-subsurface-parent-ready-for-drag");
    fflush(stdout);
    int movement_verified = 0;
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (inspect_subsurface_slots(channel, colors, probes) &&
            (probes[0].x != original_x || probes[0].y != original_y) &&
            probes[1].x == probes[0].x + 18 &&
            probes[1].y == probes[0].y + 18 &&
            probes[2].x == probes[0].x + 18 &&
            probes[2].y == probes[0].y + 18 &&
            probes[3].x == probes[0].x + 24 &&
            probes[3].y == probes[0].y + 24 &&
            (probes[1].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) == 0U &&
            (probes[2].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) != 0U &&
            (probes[3].flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) != 0U) {
            movement_verified = 1;
            break;
        }
        (void)usleep(10000U);
    }
    if (!movement_verified || wl_display_roundtrip(display) < 0) goto cleanup;
    puts("wayland-subsurface-parent-move-propagated");
    fflush(stdout);

    if (!inspect_subsurface_slots(channel, colors, probes)) goto cleanup;
    original_x = probes[0].x;
    original_y = probes[0].y;
    failure_stage = "xdg_toplevel.move pointer request";
    puts("wayland-subsurface-parent-ready-for-move-request");
    fflush(stdout);
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (__atomic_load_n(&state.move_request_sent, __ATOMIC_ACQUIRE))
            break;
        if (wl_display_dispatch(display) < 0) goto cleanup;
    }
    if (!__atomic_load_n(&state.move_request_sent, __ATOMIC_ACQUIRE))
        goto cleanup;

    int xdg_move_verified = 0;
    failure_stage = "xdg_toplevel.move position propagation";
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (inspect_subsurface_slots(channel, colors, probes) &&
            (probes[0].x != original_x || probes[0].y != original_y) &&
            probes[1].x == probes[0].x + 18 &&
            probes[1].y == probes[0].y + 18 &&
            probes[2].x == probes[0].x + 18 &&
            probes[2].y == probes[0].y + 18 &&
            probes[3].x == probes[0].x + 24 &&
            probes[3].y == probes[0].y + 24) {
            xdg_move_verified = 1;
            break;
        }
        (void)usleep(10000U);
    }
    if (!xdg_move_verified || wl_display_roundtrip(display) < 0) goto cleanup;
    puts("wayland-xdg-toplevel-move-propagated");
    fflush(stdout);

    uint32_t configure_generation = __atomic_load_n(
        &configure_state.configure_generation, __ATOMIC_ACQUIRE);
    failure_stage = "maximum constrained resize request";
    puts("wayland-subsurface-parent-ready-for-max-resize");
    fflush(stdout);
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (__atomic_load_n(&state.resize_request_count, __ATOMIC_ACQUIRE) >= 1U)
            break;
        if (wl_display_dispatch(display) < 0) goto cleanup;
    }
    if (__atomic_load_n(&state.resize_request_count, __ATOMIC_ACQUIRE) < 1U ||
        !commit_resize_configure(&state, &configure_state, display, parent,
                                 channel, colors, probes,
                                 configure_generation, 670U, 480U,
                                 0U, 0U, 0U,
                                 &resize_buffers[0]))
        goto cleanup;
    puts("wayland-toplevel-resize-max-applied");
    fflush(stdout);

    configure_generation = __atomic_load_n(
        &configure_state.configure_generation, __ATOMIC_ACQUIRE);
    failure_stage = "minimum constrained resize request";
    puts("wayland-subsurface-parent-ready-for-min-resize");
    fflush(stdout);
    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        if (__atomic_load_n(&state.resize_request_count, __ATOMIC_ACQUIRE) >= 2U)
            break;
        if (wl_display_dispatch(display) < 0) goto cleanup;
    }
    if (__atomic_load_n(&state.resize_request_count, __ATOMIC_ACQUIRE) < 2U ||
        !commit_resize_configure(&state, &configure_state, display, parent,
                                 channel, colors, probes,
                                 configure_generation, 600U, 480U,
                                 0U, 0U, 0U,
                                 &resize_buffers[1]))
        goto cleanup;
    puts("wayland-toplevel-resize-min-applied");
    fflush(stdout);

    int32_t restore_x = probes[0].x;
    int32_t restore_y = probes[0].y;
    xdg_toplevel_set_min_size(toplevel, 0, 0);
    xdg_toplevel_set_max_size(toplevel, 0, 0);
    if (wl_display_flush(display) < 0) goto cleanup;

    configure_generation = __atomic_load_n(
        &configure_state.configure_generation, __ATOMIC_ACQUIRE);
    failure_stage = "maximized state configure";
    xdg_toplevel_set_maximized(toplevel);
    if (wl_display_flush(display) < 0 ||
        !commit_resize_configure(&state, &configure_state, display, parent,
                                 channel, colors, probes,
                                 configure_generation, 1024U, 692U,
                                 TEST_CONFIGURE_MAXIMIZED,
                                 AKAOS_SURFACE_FLAG_MAXIMIZED,
                                 AKAOS_SURFACE_FLAG_FULLSCREEN,
                                 &state_buffers[0]) ||
        probes[0].x != 0 || probes[0].y != 28)
        goto cleanup;
    puts("wayland-toplevel-maximized-state-applied");
    fflush(stdout);

    configure_generation = __atomic_load_n(
        &configure_state.configure_generation, __ATOMIC_ACQUIRE);
    failure_stage = "fullscreen state configure";
    xdg_toplevel_set_fullscreen(toplevel, NULL);
    if (wl_display_flush(display) < 0 ||
        !commit_resize_configure(&state, &configure_state, display, parent,
                                 channel, colors, probes,
                                 configure_generation, 1024U, 768U,
                                 TEST_CONFIGURE_FULLSCREEN,
                                 AKAOS_SURFACE_FLAG_FULLSCREEN,
                                 AKAOS_SURFACE_FLAG_MAXIMIZED,
                                 &state_buffers[1]) ||
        probes[0].x != 0 || probes[0].y != 0)
        goto cleanup;
    puts("wayland-toplevel-fullscreen-state-applied");
    fflush(stdout);

    configure_generation = __atomic_load_n(
        &configure_state.configure_generation, __ATOMIC_ACQUIRE);
    failure_stage = "restore windowed state configure";
    xdg_toplevel_unset_fullscreen(toplevel);
    if (wl_display_flush(display) < 0 ||
        !commit_resize_configure(&state, &configure_state, display, parent,
                                 channel, colors, probes,
                                 configure_generation, 600U, 480U, 0U, 0U,
                                 AKAOS_SURFACE_FLAG_MAXIMIZED |
                                     AKAOS_SURFACE_FLAG_FULLSCREEN,
                                 &state_buffers[2]) ||
        probes[0].x != restore_x || probes[0].y != restore_y)
        goto cleanup;
    puts("wayland-toplevel-windowed-state-restored");
    fflush(stdout);

    failure_stage = "minimized state publication";
    xdg_toplevel_set_minimized(toplevel);
    if (wl_display_flush(display) < 0) goto cleanup;
    int minimized_state_applied = 0;
    for (unsigned attempt = 0U; attempt < 400U; ++attempt) {
        if (inspect_subsurface_slots(channel, colors, probes) &&
            (probes[0].flags & AKAOS_SURFACE_FLAG_MINIMIZED)) {
            minimized_state_applied = 1;
            break;
        }
        (void)usleep(10000U);
    }
    if (!minimized_state_applied) goto cleanup;
    puts("wayland-toplevel-minimized-state-applied");
    fflush(stdout);
    result = 0;

cleanup:
    if (channel && channel != MAP_FAILED)
        (void)munmap(channel, sizeof(*channel));
    if (channel_fd >= 0) close(channel_fd);
    if (toplevel) xdg_toplevel_destroy(toplevel);
    if (xdg_parent) xdg_surface_destroy(xdg_parent);
    if (state.pointer) wl_pointer_destroy(state.pointer);
    if (state.seat) wl_seat_destroy(state.seat);
    if (nested_subsurface) wl_subsurface_destroy(nested_subsurface);
    if (upper_subsurface) wl_subsurface_destroy(upper_subsurface);
    if (subsurface) wl_subsurface_destroy(subsurface);
    if (nested_child) wl_surface_destroy(nested_child);
    if (upper_child) wl_surface_destroy(upper_child);
    if (child) wl_surface_destroy(child);
    if (parent) wl_surface_destroy(parent);
    if (display) (void)wl_display_roundtrip(display);
    if (upper_buffer) wl_buffer_destroy(upper_buffer);
    if (nested_buffer) wl_buffer_destroy(nested_buffer);
    if (resize_buffers[0]) wl_buffer_destroy(resize_buffers[0]);
    if (resize_buffers[1]) wl_buffer_destroy(resize_buffers[1]);
    if (state_buffers[0]) wl_buffer_destroy(state_buffers[0]);
    if (state_buffers[1]) wl_buffer_destroy(state_buffers[1]);
    if (state_buffers[2]) wl_buffer_destroy(state_buffers[2]);
    if (child_buffer) wl_buffer_destroy(child_buffer);
    if (parent_buffer) wl_buffer_destroy(parent_buffer);
    if (state.subcompositor) wl_subcompositor_destroy(state.subcompositor);
    if (state.wm_base) xdg_wm_base_destroy(state.wm_base);
    if (state.shm) wl_shm_destroy(state.shm);
    if (state.compositor) wl_compositor_destroy(state.compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    if (result != 0) {
        fprintf(stderr,
                "Upstream Wayland smoke failed at %s (errno=%d; seat=%p caps=0x%x pointer=%p)\n",
                failure_stage, errno, (void *)state.seat,
                (unsigned int)state.seat_capabilities, (void *)state.pointer);
        return 1;
    }
    return 0;
}

static int run_foreign_toplevel_smoke(void) {
    struct wayland_smoke_state wayland = {0};
    struct foreign_toplevel_test_state foreign = {0};
    struct external_xdg_configure_state configure = {0};
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct xdg_surface *xdg_surface = NULL;
    struct xdg_toplevel *toplevel = NULL;
    struct wl_buffer *buffer = NULL;
    const char *stage = "environment";
    int result = 1;

    wayland.need_foreign = 1U;
    wayland.need_pointer = 1U;
    wayland.foreign_test = &foreign;
    if (setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0 ||
        setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0)
        goto cleanup;
    stage = "Wayland connect";
    for (unsigned attempt = 0U; attempt < 1800U && !display; ++attempt) {
        display = wl_display_connect(NULL);
        if (!display) (void)usleep(100000U);
    }
    if (!display) goto cleanup;
    registry = wl_display_get_registry(display);
    if (!registry || wl_registry_add_listener(registry, &registry_listener,
                                               &wayland) != 0)
        goto cleanup;
    stage = "registry globals";
    if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0 ||
        !wayland.compositor || !wayland.shm || !wayland.wm_base ||
        !wayland.foreign_manager || !wayland.seat ||
        !(__atomic_load_n(&wayland.seat_capabilities, __ATOMIC_ACQUIRE) &
          WL_SEAT_CAPABILITY_KEYBOARD))
        goto cleanup;
    wayland.keyboard = wl_seat_get_keyboard(wayland.seat);
    if (!wayland.keyboard || wl_keyboard_add_listener(
            wayland.keyboard, &external_keyboard_listener, &configure) != 0)
        goto cleanup;
    if (xdg_wm_base_add_listener(wayland.wm_base,
                                 &external_wm_base_listener, NULL) != 0)
        goto cleanup;

    stage = "toplevel creation";
    surface = wl_compositor_create_surface(wayland.compositor);
    if (!surface) goto cleanup;
    xdg_surface = xdg_wm_base_get_xdg_surface(wayland.wm_base, surface);
    if (!xdg_surface || xdg_surface_add_listener(
            xdg_surface, &external_xdg_surface_listener, &configure) != 0)
        goto cleanup;
    toplevel = xdg_surface_get_toplevel(xdg_surface);
    if (!toplevel || xdg_toplevel_add_listener(
            toplevel, &external_toplevel_listener, &configure) != 0)
        goto cleanup;
    xdg_toplevel_set_title(toplevel, "akaOS Foreign Toplevel Test");
    xdg_toplevel_set_app_id(toplevel, "akaos-foreign-toplevel-test");
    wl_surface_commit(surface);
    stage = "initial configure";
    if (wl_display_roundtrip(display) < 0 ||
        !__atomic_load_n(&configure.configured, __ATOMIC_ACQUIRE) ||
        configure.width <= 0 || configure.height <= 0)
        goto cleanup;

    stage = "window mapping";
    buffer = create_external_test_buffer(&wayland,
        (uint32_t)configure.width, (uint32_t)configure.height,
        UINT32_C(0xff406080));
    if (!buffer) goto cleanup;
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, configure.width, configure.height);
    wl_surface_commit(surface);
    if (wl_display_roundtrip(display) < 0 || !foreign.handle ||
        !foreign.saw_title || !foreign.saw_app_id || !foreign.saw_done)
        goto cleanup;
    puts("wayland-foreign-toplevel-metadata-verified");
    fflush(stdout);
    foreign.saw_done = 0;
    zwlr_foreign_toplevel_handle_v1_activate(foreign.handle, wayland.seat);
    if (wl_display_roundtrip(display) < 0 || !foreign.saw_done ||
        !foreign.activated ||
        !(configure.toplevel_states & TEST_CONFIGURE_ACTIVATED))
        goto cleanup;
    puts("wayland-foreign-toplevel-activate-focus-verified");
    fflush(stdout);
    (void)usleep(45000000U);

    stage = "minimize request";
    foreign.saw_done = 0;
    zwlr_foreign_toplevel_handle_v1_set_minimized(foreign.handle);
    if (wl_display_roundtrip(display) < 0 || !foreign.saw_done ||
        !foreign.minimized)
        goto cleanup;
    foreign.saw_done = 0;
    zwlr_foreign_toplevel_handle_v1_unset_minimized(foreign.handle);
    if (wl_display_roundtrip(display) < 0 || !foreign.saw_done ||
        foreign.minimized)
        goto cleanup;
    stage = "close request";
    zwlr_foreign_toplevel_handle_v1_close(foreign.handle);
    if (wl_display_roundtrip(display) < 0 ||
        !__atomic_load_n(&configure.close_received, __ATOMIC_ACQUIRE))
        goto cleanup;

    stage = "unmap notification";
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
    if (wl_display_roundtrip(display) < 0 || !foreign.saw_closed)
        goto cleanup;
    puts("wayland-foreign-toplevel-lifecycle-and-actions-verified");
    fflush(stdout);
    result = 0;

cleanup:
    if (foreign.handle)
        zwlr_foreign_toplevel_handle_v1_destroy(foreign.handle);
    if (foreign.manager) {
        zwlr_foreign_toplevel_manager_v1_stop(foreign.manager);
        if (display) (void)wl_display_roundtrip(display);
        zwlr_foreign_toplevel_manager_v1_destroy(foreign.manager);
    }
    if (toplevel) xdg_toplevel_destroy(toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (surface) wl_surface_destroy(surface);
    if (display) (void)wl_display_roundtrip(display);
    if (buffer) wl_buffer_destroy(buffer);
    if (wayland.wm_base) xdg_wm_base_destroy(wayland.wm_base);
    if (wayland.keyboard) wl_keyboard_destroy(wayland.keyboard);
    if (wayland.seat) wl_seat_destroy(wayland.seat);
    if (wayland.shm) wl_shm_destroy(wayland.shm);
    if (wayland.compositor) wl_compositor_destroy(wayland.compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    if (result != 0)
        fprintf(stderr, "Wayland foreign-toplevel smoke failed at %s\n", stage);
    return result;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--external-subsurface-smoke") == 0)
        return run_external_subsurface_smoke();
    if (argc == 2 && strcmp(argv[1], "--foreign-toplevel-smoke") == 0)
        return run_foreign_toplevel_smoke();
    if (argc != 1) {
        puts("Usage: wayland-upstream-test [--external-subsurface-smoke|--foreign-toplevel-smoke]");
        return 2;
    }
    struct wayland_smoke_state state = {0};
    struct wl_registry *registry = NULL;
    struct wl_display *client_display = NULL;
    struct wl_shm_pool *pool = NULL;
    pthread_t server_thread;
    struct wl_listener client_created_listener = {
        .notify = client_created_notify,
    };
    int thread_started = 0;
    int memfd = -1;
    int step = 0;
    int result = 1;

    step = 1;
    if (setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0 ||
        setenv("WAYLAND_DEBUG", "client,server", 1) != 0) goto cleanup;
    step = 2;
    state.server = wl_display_create();
    if (!state.server) goto cleanup;
    step = 3;
    if (wl_display_init_shm(state.server) != 0) goto cleanup;
    step = 4;
    if (!wl_global_create(state.server, &wl_compositor_interface, 4, &state,
                          bind_compositor)) goto cleanup;
    listener_state = &state;
    wl_display_add_client_created_listener(state.server,
                                           &client_created_listener);
    step = 5;
    if (wl_display_add_socket(state.server, "akaos-wayland-smoke") != 0)
        goto cleanup;
    step = 6;
    if (pthread_create(&server_thread, NULL, run_server, &state) != 0)
        goto cleanup;
    thread_started = 1;
    step = 7;
    client_display = wl_display_connect("akaos-wayland-smoke");
    if (!client_display) goto cleanup;

    step = 8;
    errno = 0;
    registry = wl_display_get_registry(client_display);
    if (!registry) {
        fprintf(stderr, "wl_display_get_registry failed: fd=%d error=%d "
                "errno=%d clients-created=%u\n",
                wl_display_get_fd(client_display),
                wl_display_get_error(client_display), errno,
                __atomic_load_n(&state.clients_created, __ATOMIC_ACQUIRE));
        goto cleanup;
    }
    step = 9;
    if (wl_registry_add_listener(registry, &registry_listener, &state) != 0)
        goto cleanup;
    step = 10;
    errno = 0;
    if (wl_display_roundtrip(client_display) < 0) goto cleanup;
    step = 11;
    if (!__atomic_load_n(&state.saw_compositor, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&state.saw_shm, __ATOMIC_ACQUIRE)) goto cleanup;
    step = 12;
    errno = 0;
    if (wl_display_roundtrip(client_display) < 0 ||
        __atomic_load_n(&state.compositor_bound, __ATOMIC_ACQUIRE) != 1U)
        goto cleanup;

    step = 13;
    memfd = memfd_create("akaos-wayland-shm", MFD_CLOEXEC);
    if (memfd < 0 || ftruncate(memfd, 4096) != 0) goto cleanup;
    step = 14;
    pool = wl_shm_create_pool(state.shm, memfd, 4096);
    close(memfd);
    memfd = -1;
    if (!pool || wl_display_roundtrip(client_display) < 0) goto cleanup;
    step = 15;
    wl_shm_pool_destroy(pool);
    pool = NULL;
    if (wl_display_roundtrip(client_display) < 0) goto cleanup;

    result = 0;

cleanup:
    if (memfd >= 0) close(memfd);
    if (pool) wl_shm_pool_destroy(pool);
    if (client_display) {
        if (state.subcompositor)
            wl_subcompositor_destroy(state.subcompositor);
        if (state.compositor) wl_compositor_destroy(state.compositor);
        if (state.shm) wl_shm_destroy(state.shm);
        if (registry) wl_registry_destroy(registry);
        wl_display_disconnect(client_display);
    }
    if (state.server) {
        wl_display_terminate(state.server);
        if (thread_started) pthread_join(server_thread, NULL);
        wl_display_destroy_clients(state.server);
        wl_display_destroy(state.server);
    }
    listener_state = NULL;
    if (result != 0) {
        fprintf(stderr,
                "Upstream Wayland client/server SHM smoke failed at %d "
                "(errno=%d)\n", step, errno);
        return 1;
    }
    puts("Upstream Wayland 1.26 registry binding and memfd SHM verified");
    return 0;
}
