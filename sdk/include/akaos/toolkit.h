#ifndef AKAOS_TOOLKIT_H
#define AKAOS_TOOLKIT_H

#include <akaos/display.h>

/* Stable bridge used by GTK/Qt ports.  It keeps toolkit code independent of
 * the compositor implementation while preserving the existing shared-memory
 * surface protocol. */
#define AKAOS_TOOLKIT_ABI_VERSION UINT32_C(1)
#define AKAOS_GTK_COMPAT_VERSION "4.0-akaos"
#define AKAOS_QT_COMPAT_VERSION "6.0-akaos"

struct akaos_toolkit_surface {
    struct akaos_compositor_channel *channel;
    uint32_t surface_id;
    uint32_t client_pid;
    uint32_t next_configure_serial;
};

int akaos_toolkit_connect(struct akaos_compositor_channel **channel_out);
int akaos_toolkit_display_info(struct akaos_display_info *info);
int akaos_toolkit_disconnect(struct akaos_compositor_channel *channel);
int akaos_toolkit_surface_attach(struct akaos_toolkit_surface *surface,
                                 struct akaos_compositor_channel *channel,
                                 uint32_t surface_id, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 const char *shm_name, const char *title,
                                 const char *icon_name);
int akaos_toolkit_surface_damage(const struct akaos_toolkit_surface *surface,
                                 uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height);
int akaos_toolkit_surface_move(const struct akaos_toolkit_surface *surface,
                               int32_t x, int32_t y);
int akaos_toolkit_surface_resize(struct akaos_toolkit_surface *surface,
                                 int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 const char *shm_name);
uint32_t akaos_toolkit_surface_configure_serial(
    const struct akaos_toolkit_surface *surface);
int akaos_toolkit_surface_wait_configure(
    const struct akaos_toolkit_surface *surface, uint32_t serial,
    uint32_t timeout_ms);
int akaos_toolkit_surface_ack_configure(
    const struct akaos_toolkit_surface *surface, uint32_t serial);
int akaos_toolkit_surface_wait_configure_ack(
    const struct akaos_toolkit_surface *surface, uint32_t serial,
    uint32_t timeout_ms);
int akaos_toolkit_surface_focus(const struct akaos_toolkit_surface *surface);
int akaos_toolkit_surface_destroy(const struct akaos_toolkit_surface *surface);

#endif
