/*
 * akaOS XSCore Lumen Wayland Compositor - Core Protocol & IPC
 *
 * Implements standard Wayland wire protocol framing, interface registry,
 * and xdg-shell extension for hardware-accelerated desktop compositing.
 */

#ifndef LUMEN_WAYLAND_PROTOCOL_H
#define LUMEN_WAYLAND_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* Wayland Wire Protocol Header (8 bytes) */
struct wayland_message_header {
    uint32_t object_id;
    uint16_t opcode;
    uint16_t length;
};

/* Core Wayland Interface IDs */
#define WL_DISPLAY_ID       1U
#define WL_REGISTRY_ID      2U
#define WL_COMPOSITOR_ID    3U
#define WL_SHM_ID           4U
#define WL_OUTPUT_ID        5U
#define WL_SEAT_ID          6U

/* Core Wayland OpCodes */
#define WL_DISPLAY_SYNC                 0U
#define WL_DISPLAY_GET_REGISTRY         1U
#define WL_DISPLAY_ERROR                0U
#define WL_DISPLAY_DELETE_ID            1U

#define WL_REGISTRY_BIND                0U
#define WL_REGISTRY_GLOBAL              0U
#define WL_REGISTRY_GLOBAL_REMOVE       1U

#define WL_COMPOSITOR_CREATE_SURFACE    0U
#define WL_COMPOSITOR_CREATE_REGION     1U

#define WL_SURFACE_DESTROY              0U
#define WL_SURFACE_ATTACH               1U
#define WL_SURFACE_DAMAGE               2U
#define WL_SURFACE_FRAME                3U
#define WL_SURFACE_SET_OPAQUE_REGION    4U
#define WL_SURFACE_SET_INPUT_REGION     5U
#define WL_SURFACE_COMMIT               6U
#define WL_SURFACE_SET_BUFFER_TRANSFORM 7U
#define WL_SURFACE_SET_BUFFER_SCALE     8U
#define WL_SURFACE_DAMAGE_BUFFER        9U

#define WL_SHM_CREATE_POOL              0U
#define WL_SHM_FORMAT                   0U

#define WL_SHM_FORMAT_ARGB8888          0U
#define WL_SHM_FORMAT_XRGB8888          1U

/* XDG Shell Extension OpCodes */
#define XDG_WM_BASE_DESTROY             0U
#define XDG_WM_BASE_CREATE_POSITIONER   1U
#define XDG_WM_BASE_GET_XDG_SURFACE     2U
#define XDG_WM_BASE_PONG                3U
#define XDG_WM_BASE_PING                0U

#define XDG_SURFACE_DESTROY             0U
#define XDG_SURFACE_GET_TOPLEVEL        1U
#define XDG_SURFACE_GET_POPUP           2U
#define XDG_SURFACE_SET_WINDOW_GEOMETRY 3U
#define XDG_SURFACE_ACK_CONFIGURE       4U
#define XDG_SURFACE_CONFIGURE           0U

#define XDG_TOPLEVEL_DESTROY            0U
#define XDG_TOPLEVEL_SET_PARENT         1U
#define XDG_TOPLEVEL_SET_TITLE          2U
#define XDG_TOPLEVEL_SET_APP_ID         3U
#define XDG_TOPLEVEL_SHOW_WINDOW_MENU   4U
#define XDG_TOPLEVEL_MOVE               5U
#define XDG_TOPLEVEL_RESIZE             6U
#define XDG_TOPLEVEL_SET_MAX_SIZE       7U
#define XDG_TOPLEVEL_SET_MIN_SIZE       8U
#define XDG_TOPLEVEL_SET_MAXIMIZED      9U
#define XDG_TOPLEVEL_UNSET_MAXIMIZED    10U
#define XDG_TOPLEVEL_SET_FULLSCREEN     11U
#define XDG_TOPLEVEL_UNSET_FULLSCREEN   12U
#define XDG_TOPLEVEL_SET_MINIMIZED      13U

/* Surface damage rectangle */
struct lumen_damage_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

/* Wayland client buffer descriptor */
struct lumen_buffer {
    uint32_t id;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint32_t format;
    uint32_t gem_handle;        /* Imported DRM GEM handle if DMA-BUF */
    void *shm_data;             /* Mapped SHM buffer if wl_shm */
    uint64_t dma_buf_paddr;     /* Physical address for direct DMA */
    uint32_t is_dmabuf;
    uint32_t refcount;
};

/* Wayland surface representation */
struct lumen_surface {
    uint32_t id;
    uint32_t client_id;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    struct lumen_buffer *current_buffer;
    struct lumen_buffer *pending_buffer;
    struct lumen_damage_rect damage;
    uint32_t damage_seq;
    uint32_t is_mapped;
    uint32_t is_toplevel;
    char title[128];
    char app_id[64];
    struct lumen_surface *next;
};

/* Wayland connection context */
struct lumen_client {
    uint32_t id;
    int socket_fd;
    uint32_t next_object_id;
    struct lumen_surface *surfaces;
    struct lumen_client *next;
};

/* Protocol API */
int lumen_protocol_init(void);
void lumen_protocol_destroy(void);
struct lumen_client *lumen_client_create(int socket_fd);
void lumen_client_destroy(struct lumen_client *client);
int lumen_client_dispatch(struct lumen_client *client);
void lumen_surface_commit(struct lumen_surface *surface);

#endif /* LUMEN_WAYLAND_PROTOCOL_H */
