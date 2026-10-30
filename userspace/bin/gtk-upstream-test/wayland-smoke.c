#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <gtk/gtk.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-server-protocol.h"

extern int akaos_gtk_set_wayland_environment(const char *display_name,
                                              int debug, int trace);

#define GTK_WAYLAND_TRACE(message) do { \
    printf("[GTK-WAYLAND] %s\n", (message)); \
    fflush(stdout); \
} while (0)

struct gtk_wayland_server;
struct gtk_wayland_xdg_surface;

struct gtk_wayland_surface {
    struct gtk_wayland_server *server;
    struct gtk_wayland_xdg_surface *xdg_surface;
    struct wl_resource *pending_buffer;
    struct wl_resource *frame_callback;
};

struct gtk_wayland_xdg_surface {
    struct gtk_wayland_server *server;
    struct gtk_wayland_surface *surface;
    struct wl_resource *resource;
    struct wl_resource *toplevel;
    int configured;
};

struct gtk_wayland_server {
    struct wl_display *display;
    uint32_t buffer_committed;
};

struct registry_probe {
    int saw_compositor;
    int saw_shm;
    int saw_wm_base;
};

static void probe_global(void *data, struct wl_registry *registry,
                         uint32_t name, const char *interface,
                         uint32_t version) {
    (void)registry; (void)name; (void)version;
    struct registry_probe *probe = data;
    if (strcmp(interface, "wl_compositor") == 0)
        probe->saw_compositor = 1;
    else if (strcmp(interface, "wl_shm") == 0)
        probe->saw_shm = 1;
    else if (strcmp(interface, "xdg_wm_base") == 0)
        probe->saw_wm_base = 1;
}

static void probe_global_remove(void *data, struct wl_registry *registry,
                                uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_probe_listener = {
    .global = probe_global,
    .global_remove = probe_global_remove,
};

static const struct xdg_surface_interface xdg_surface_implementation;
static const struct xdg_toplevel_interface toplevel_implementation;

static void destroy_resource(struct wl_client *client,
                             struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client,
                       struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static void region_subtract(struct wl_client *client,
                            struct wl_resource *resource,
                            int32_t x, int32_t y, int32_t width, int32_t height) {
    region_add(client, resource, x, y, width, height);
}

static const struct wl_region_interface region_implementation = {
    .destroy = destroy_resource,
    .add = region_add,
    .subtract = region_subtract,
};

static void surface_destroyed(struct wl_resource *resource) {
    struct gtk_wayland_surface *surface = wl_resource_get_user_data(resource);
    if (!surface) return;
    if (surface->xdg_surface)
        surface->xdg_surface->surface = NULL;
    if (surface->frame_callback)
        wl_resource_destroy(surface->frame_callback);
    free(surface);
}

static void surface_attach(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *buffer,
                           int32_t x, int32_t y) {
    (void)client; (void)x; (void)y;
    struct gtk_wayland_surface *surface = wl_resource_get_user_data(resource);
    GTK_WAYLAND_TRACE(buffer ? "server attached a wl_buffer"
                             : "server detached the wl_buffer");
    surface->pending_buffer = buffer;
}

static void surface_damage(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y,
                           int32_t width, int32_t height) {
    (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static void surface_frame(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t callback_id) {
    struct gtk_wayland_surface *surface = wl_resource_get_user_data(resource);
    struct wl_resource *callback = wl_resource_create(
        client, &wl_callback_interface, 1, callback_id);
    if (!callback) {
        wl_client_post_no_memory(client);
        return;
    }
    if (surface->frame_callback)
        wl_resource_destroy(surface->frame_callback);
    wl_resource_set_implementation(callback, NULL, NULL, NULL);
    surface->frame_callback = callback;
}

static void surface_set_region(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *region) {
    (void)client; (void)resource; (void)region;
}

static void send_initial_configure(struct gtk_wayland_xdg_surface *xdg) {
    struct wl_array states;
    wl_array_init(&states);
    xdg_toplevel_send_configure(xdg->toplevel, 640, 480, &states);
    xdg_surface_send_configure(xdg->resource,
                               wl_display_next_serial(xdg->server->display));
    wl_array_release(&states);
    xdg->configured = 1;
}

static void surface_commit(struct wl_client *client,
                           struct wl_resource *resource) {
    (void)client;
    GTK_WAYLAND_TRACE("test server received wl_surface.commit");
    struct gtk_wayland_surface *surface = wl_resource_get_user_data(resource);
    if (surface->xdg_surface && surface->xdg_surface->toplevel &&
        !surface->xdg_surface->configured)
        send_initial_configure(surface->xdg_surface);

    if (surface->pending_buffer) {
        GTK_WAYLAND_TRACE("test server observed a committed buffer");
        __atomic_store_n(&surface->server->buffer_committed, 1U,
                         __ATOMIC_RELEASE);
        wl_buffer_send_release(surface->pending_buffer);
        surface->pending_buffer = NULL;
    }
    if (surface->frame_callback) {
        wl_callback_send_done(surface->frame_callback, 0U);
        wl_resource_destroy(surface->frame_callback);
        surface->frame_callback = NULL;
    }
}

static void surface_transform(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t transform) {
    (void)client; (void)resource; (void)transform;
}

static void surface_scale(struct wl_client *client,
                          struct wl_resource *resource,
                          int32_t scale) {
    (void)client; (void)resource; (void)scale;
}

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y) {
    (void)client; (void)resource; (void)x; (void)y;
}

static const struct wl_surface_interface surface_implementation = {
    .destroy = destroy_resource,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_set_region,
    .set_input_region = surface_set_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_transform,
    .set_buffer_scale = surface_scale,
    .damage_buffer = surface_damage,
    .offset = surface_offset,
};

static void compositor_create_surface(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t id) {
    GTK_WAYLAND_TRACE("server received wl_compositor.create_surface");
    struct gtk_wayland_server *server = wl_resource_get_user_data(resource);
    struct wl_resource *surface_resource = wl_resource_create(
        client, &wl_surface_interface, 4, id);
    struct gtk_wayland_surface *surface = calloc(1U, sizeof(*surface));
    if (!surface_resource || !surface) {
        if (surface_resource) wl_resource_destroy(surface_resource);
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    surface->server = server;
    wl_resource_set_implementation(surface_resource, &surface_implementation,
                                   surface, surface_destroyed);
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
        client, &wl_compositor_interface, version < 4U ? (int)version : 4, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_implementation,
                                   data, NULL);
}

static void output_release(struct wl_client *client,
                           struct wl_resource *resource) {
    destroy_resource(client, resource);
}

static const struct wl_output_interface output_implementation = {
    .release = output_release,
};

/* GTK requires this global to accept a Wayland display. The smoke does not
 * create a seat or exercise clipboard/data-device requests. */
static const struct wl_data_device_manager_interface data_device_manager_implementation = {0};

static void bind_output(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &wl_output_interface, version < 2U ? (int)version : 2, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &output_implementation, data, NULL);
    wl_output_send_geometry(resource, 0, 0, 600, 340,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN, "akaOS", "QEMU test",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(resource,
                        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        640, 480, 60000);
    wl_output_send_scale(resource, 1);
    if (version >= 2U) wl_output_send_done(resource);
}

static void bind_data_device_manager(struct wl_client *client, void *data,
                                    uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &wl_data_device_manager_interface,
        version < 1U ? (int)version : 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &data_device_manager_implementation,
                                   data, NULL);
}

static void xdg_positioner_destroy(struct wl_client *client,
                                   struct wl_resource *resource) {
    destroy_resource(client, resource);
}

static void positioner_size(struct wl_client *client, struct wl_resource *res,
                            int32_t width, int32_t height) {
    (void)client; (void)res; (void)width; (void)height;
}

static void positioner_anchor_rect(struct wl_client *client,
                                   struct wl_resource *res,
                                   int32_t x, int32_t y,
                                   int32_t width, int32_t height) {
    (void)client; (void)res; (void)x; (void)y; (void)width; (void)height;
}

static void positioner_value(struct wl_client *client,
                             struct wl_resource *res, uint32_t value) {
    (void)client; (void)res; (void)value;
}

static void positioner_offset(struct wl_client *client,
                              struct wl_resource *res,
                              int32_t x, int32_t y) {
    (void)client; (void)res; (void)x; (void)y;
}

static void positioner_parent_size(struct wl_client *client,
                                   struct wl_resource *res,
                                   int32_t width, int32_t height) {
    (void)client; (void)res; (void)width; (void)height;
}

static void positioner_reactive(struct wl_client *client,
                                struct wl_resource *res) {
    (void)client; (void)res;
}

static void positioner_parent_configure(struct wl_client *client,
                                        struct wl_resource *res,
                                        uint32_t serial) {
    (void)client; (void)res; (void)serial;
}

static const struct xdg_positioner_interface positioner_implementation = {
    .destroy = xdg_positioner_destroy,
    .set_size = positioner_size,
    .set_anchor_rect = positioner_anchor_rect,
    .set_anchor = positioner_value,
    .set_gravity = positioner_value,
    .set_constraint_adjustment = positioner_value,
    .set_offset = positioner_offset,
    .set_reactive = positioner_reactive,
    .set_parent_size = positioner_parent_size,
    .set_parent_configure = positioner_parent_configure,
};

static void wm_base_destroyed(struct wl_client *client,
                              struct wl_resource *resource) {
    destroy_resource(client, resource);
}

static void wm_base_create_positioner(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t id) {
    (void)resource;
    struct wl_resource *positioner = wl_resource_create(
        client, &xdg_positioner_interface, 1, id);
    if (!positioner) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(positioner, &positioner_implementation,
                                   NULL, NULL);
}

static void xdg_surface_destroyed(struct wl_resource *resource) {
    struct gtk_wayland_xdg_surface *xdg = wl_resource_get_user_data(resource);
    if (!xdg) return;
    if (xdg->surface && xdg->surface->xdg_surface == xdg)
        xdg->surface->xdg_surface = NULL;
    free(xdg);
}

static void wm_base_get_xdg_surface(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t id,
                                    struct wl_resource *surface_resource) {
    GTK_WAYLAND_TRACE("server received xdg_wm_base.get_xdg_surface");
    struct gtk_wayland_server *server = wl_resource_get_user_data(resource);
    struct gtk_wayland_surface *surface =
        wl_resource_get_user_data(surface_resource);
    struct gtk_wayland_xdg_surface *xdg = calloc(1U, sizeof(*xdg));
    struct wl_resource *xdg_resource = wl_resource_create(
        client, &xdg_surface_interface, 1, id);
    if (!xdg || !xdg_resource) {
        free(xdg);
        if (xdg_resource) wl_resource_destroy(xdg_resource);
        wl_client_post_no_memory(client);
        return;
    }
    xdg->server = server;
    xdg->surface = surface;
    xdg->resource = xdg_resource;
    surface->xdg_surface = xdg;
    wl_resource_set_implementation(xdg_resource, &xdg_surface_implementation, xdg,
                                   xdg_surface_destroyed);
}

static void wm_base_pong(struct wl_client *client,
                         struct wl_resource *resource,
                         uint32_t serial) {
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_wm_base_interface wm_base_implementation = {
    .destroy = wm_base_destroyed,
    .create_positioner = wm_base_create_positioner,
    .get_xdg_surface = wm_base_get_xdg_surface,
    .pong = wm_base_pong,
};

static void xdg_surface_destroy(struct wl_client *client,
                                struct wl_resource *resource) {
    destroy_resource(client, resource);
}

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
    GTK_WAYLAND_TRACE("server received xdg_surface.get_toplevel");
    struct gtk_wayland_xdg_surface *xdg = wl_resource_get_user_data(resource);
    struct wl_resource *toplevel = wl_resource_create(
        client, &xdg_toplevel_interface, 1, id);
    if (!toplevel) {
        wl_client_post_no_memory(client);
        return;
    }
    xdg->toplevel = toplevel;
    wl_resource_set_implementation(toplevel, &toplevel_implementation, xdg,
                                   NULL);
}

static void xdg_surface_geometry(struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
    (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static void xdg_surface_ack_configure(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t serial) {
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_surface_interface xdg_surface_implementation = {
    .destroy = xdg_surface_destroy,
    .get_toplevel = xdg_surface_get_toplevel,
    .set_window_geometry = xdg_surface_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

static void toplevel_destroy(struct wl_client *client,
                             struct wl_resource *resource) {
    struct gtk_wayland_xdg_surface *xdg = wl_resource_get_user_data(resource);
    if (xdg) xdg->toplevel = NULL;
    destroy_resource(client, resource);
}

static void toplevel_parent(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *parent) {
    (void)client; (void)resource; (void)parent;
}

static void toplevel_string(struct wl_client *client,
                            struct wl_resource *resource,
                            const char *value) {
    (void)client; (void)resource; (void)value;
}

static void toplevel_menu(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial,
                          int32_t x, int32_t y) {
    (void)client; (void)resource; (void)seat; (void)serial; (void)x; (void)y;
}

static void toplevel_move(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial) {
    (void)client; (void)resource; (void)seat; (void)serial;
}

static void toplevel_resize(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial,
                            uint32_t edges) {
    (void)client; (void)resource; (void)seat; (void)serial; (void)edges;
}

static void toplevel_size(struct wl_client *client,
                          struct wl_resource *resource,
                          int32_t width, int32_t height) {
    (void)client; (void)resource; (void)width; (void)height;
}

static void toplevel_state(struct wl_client *client,
                           struct wl_resource *resource) {
    (void)client; (void)resource;
}

static void toplevel_fullscreen(struct wl_client *client,
                                struct wl_resource *resource,
                                struct wl_resource *output) {
    (void)client; (void)resource; (void)output;
}

static const struct xdg_toplevel_interface toplevel_implementation = {
    .destroy = toplevel_destroy,
    .set_parent = toplevel_parent,
    .set_title = toplevel_string,
    .set_app_id = toplevel_string,
    .show_window_menu = toplevel_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_size,
    .set_min_size = toplevel_size,
    .set_maximized = toplevel_state,
    .unset_maximized = toplevel_state,
    .set_fullscreen = toplevel_fullscreen,
    .unset_fullscreen = toplevel_state,
    .set_minimized = toplevel_state,
};

static void bind_wm_base(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id) {
    (void)version;
    struct wl_resource *resource = wl_resource_create(
        client, &xdg_wm_base_interface, 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &wm_base_implementation,
                                   data, NULL);
}

static void *run_server(void *data) {
    struct gtk_wayland_server *server = data;
    wl_display_run(server->display);
    return NULL;
}

int run_gtk_wayland_window_smoke(void) {
    struct gtk_wayland_server server = {0};
    GtkWidget *window = NULL;
    pthread_t thread;
    int thread_started = 0;
    int result = 1;

    GTK_WAYLAND_TRACE("smoke entry");
    if (!akaos_gtk_set_wayland_environment("akaos-gtk-smoke", 1, 1)) {
        GTK_WAYLAND_TRACE("process environment setup failed");
        return 1;
    }
    GTK_WAYLAND_TRACE("process environment installed with setenv");

    server.display = wl_display_create();
    GTK_WAYLAND_TRACE("wl_display_create returned");
    if (!server.display || wl_display_init_shm(server.display) != 0)
        goto cleanup;
    GTK_WAYLAND_TRACE("wl_display_init_shm returned");
    if (!wl_global_create(server.display, &wl_compositor_interface, 4,
                          &server, bind_compositor) ||
        !wl_global_create(server.display, &wl_output_interface, 2,
                          &server, bind_output) ||
        !wl_global_create(server.display, &wl_data_device_manager_interface, 1,
                          &server, bind_data_device_manager) ||
        !wl_global_create(server.display, &xdg_wm_base_interface, 1,
                          &server, bind_wm_base))
        goto cleanup;
    GTK_WAYLAND_TRACE("compositor globals created");
    if (wl_display_add_socket(server.display, "akaos-gtk-smoke") != 0)
        goto cleanup;
    GTK_WAYLAND_TRACE("Wayland socket created");
    if (pthread_create(&thread, NULL, run_server, &server) != 0)
        goto cleanup;
    thread_started = 1;
    GTK_WAYLAND_TRACE("Wayland server thread started");

    struct wl_display *probe_display = wl_display_connect(NULL);
    if (!probe_display) {
        printf("[GTK-WAYLAND] direct client connect failed, errno=%d\n", errno);
        goto cleanup;
    }
    struct wl_registry *probe_registry = wl_display_get_registry(probe_display);
    struct registry_probe probe = {0};
    if (!probe_registry ||
        wl_registry_add_listener(probe_registry, &registry_probe_listener,
                                 &probe) != 0 ||
        wl_display_roundtrip(probe_display) < 0) {
        printf("[GTK-WAYLAND] registry roundtrip failed, error=%d\n",
               wl_display_get_error(probe_display));
        if (probe_registry) wl_registry_destroy(probe_registry);
        wl_display_disconnect(probe_display);
        goto cleanup;
    }
    if (!probe.saw_compositor || !probe.saw_shm || !probe.saw_wm_base) {
        printf("[GTK-WAYLAND] test compositor globals missing: compositor=%d shm=%d xdg=%d\n",
               probe.saw_compositor, probe.saw_shm, probe.saw_wm_base);
        goto cleanup;
    }
    wl_registry_destroy(probe_registry);
    wl_display_disconnect(probe_display);
    GTK_WAYLAND_TRACE("direct Wayland client registry probe passed and closed");

    struct wl_display *second_probe_display = wl_display_connect(NULL);
    if (!second_probe_display) {
        printf("[GTK-WAYLAND] second direct client connect failed, errno=%d\n",
               errno);
        goto cleanup;
    }
    struct wl_registry *second_probe_registry =
        wl_display_get_registry(second_probe_display);
    struct registry_probe second_probe = {0};
    if (!second_probe_registry ||
        wl_registry_add_listener(second_probe_registry,
                                 &registry_probe_listener,
                                 &second_probe) != 0 ||
        wl_display_roundtrip(second_probe_display) < 0 ||
        !second_probe.saw_compositor || !second_probe.saw_shm ||
        !second_probe.saw_wm_base) {
        printf("[GTK-WAYLAND] second direct registry probe failed, errno=%d\n",
               errno);
        if (second_probe_registry) wl_registry_destroy(second_probe_registry);
        wl_display_disconnect(second_probe_display);
        goto cleanup;
    }
    wl_registry_destroy(second_probe_registry);
    wl_display_disconnect(second_probe_display);
    GTK_WAYLAND_TRACE("second direct Wayland client registry probe passed");

    GTK_WAYLAND_TRACE("calling gtk_init_check");
    if (!gtk_init_check()) {
        printf("[GTK-WAYLAND] gtk_init_check returned false: display=%p errno=%d\n",
               (void *)gdk_display_get_default(), errno);
        goto cleanup;
    }
    GTK_WAYLAND_TRACE("gtk_init_check succeeded");
    window = gtk_window_new();
    if (!window) goto cleanup;
    GTK_WAYLAND_TRACE("GtkWindow created");
    gtk_window_set_title(GTK_WINDOW(window), "akaOS Wayland smoke");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 120);
    gtk_window_set_child(GTK_WINDOW(window),
                         gtk_label_new("GTK on the akaOS Wayland stack"));
    gtk_window_present(GTK_WINDOW(window));
    GTK_WAYLAND_TRACE("GtkWindow presented");

    for (unsigned attempt = 0U;
         attempt < 1000U &&
         __atomic_load_n(&server.buffer_committed, __ATOMIC_ACQUIRE) == 0U;
         ++attempt) {
        while (g_main_context_iteration(NULL, FALSE)) { }
        (void)usleep(10000U);
    }
    if (__atomic_load_n(&server.buffer_committed, __ATOMIC_ACQUIRE) != 1U)
        goto cleanup;
    GTK_WAYLAND_TRACE("SHM buffer commit observed");
    puts("Upstream GTK Wayland surface committed a Cairo/SHM buffer");
    puts("upstream-gtk-wayland-window-committed");
    result = 0;

cleanup:
    GTK_WAYLAND_TRACE("cleanup started");
    if (window) {
        gtk_window_destroy(GTK_WINDOW(window));
        while (g_main_context_iteration(NULL, FALSE)) { }
    }
    if (server.display) {
        wl_display_terminate(server.display);
        if (thread_started) pthread_join(thread, NULL);
        wl_display_destroy_clients(server.display);
        wl_display_destroy(server.display);
    }
    if (result != 0)
        fputs("Upstream GTK Wayland window/SHM smoke failed\n", stderr);
    return result;
}
