/* Shared server state for the akaOS X11 display server. */
#ifndef AKAOS_XSERVER_SERVER_H
#define AKAOS_XSERVER_SERVER_H

#include "proto.h"
#include <sys/time.h>

#define X_MAX_CLIENTS 8
#define X_MAX_WINDOWS 512
#define X_MAX_PIXMAPS 512
#define X_MAX_GCS 128
#define X_MAX_ATOMS 512
#define X_MAX_PROPERTIES 512
#define X_MAX_FONTS 16
#define X_MAX_COLORMAPS 16
#define X_MAX_CURSORS 64
#define X_MAX_SELECTIONS 32
#define X_MAX_EVENT_QUEUE 256
#define X_MAX_KEY_GRABS 64
/* Every client may select events on a window, so a window holds one entry
 * per connected client. */
#define X_MAX_WINDOW_SELECTIONS 8

#define X_SCREEN_WIDTH 1024
#define X_SCREEN_HEIGHT 768

/* The time in milliseconds since the epoch.
 *
 * The trace of the display server carries it, so that the time a client waits
 * for an answer is visible in the log of the guest. */
static inline uint64_t server_now_millis(void) {
    struct timeval value;
    if (gettimeofday(&value, NULL) != 0) return 0U;
    return (uint64_t)value.tv_sec * 1000U + (uint64_t)value.tv_usec / 1000U;
}

/* Whether the display server writes a trace of every request and reply.
 *
 * The console of the guest is slow, and a trace of every reply slows the
 * display server down so much that a client waits seconds for an answer.  The
 * trace is therefore only written when it is asked for. */
extern int server_trace_enabled;

/* Total backing-store budget shared by all mapped windows and pixmaps. */
#define X_BACKING_POOL_BYTES (64u * 1024u * 1024u)

#define X_INPUT_BUFFER_SIZE (256u * 1024u)
#define X_OUTPUT_BUFFER_SIZE (256u * 1024u)

#define X_ROOT_WINDOW_ID 1u
#define X_ROOT_VISUAL_ID 0x21u
#define X_ROOT_DEPTH 24

/* One client selection on a window.
 *
 * X11 event selection belongs to a (client, window) pair, so the server keeps
 * the mask of every client that selected events on a window.  A window
 * manager selects SubstructureRedirectMask on the root window, which it does
 * not own, so a single mask per window could never reach it. */
struct x_window_selection {
    int client;            /* client index, -1 for an unused slot */
    uint32_t mask;
};

struct x_window {
    xid_t id;
    int client;            /* owning client index, -1 for the root window */
    int parent;            /* index of the parent window, -1 for the root */
    int first_child;
    int next_sibling;
    int prev_sibling;
    int16_t x, y;          /* origin relative to the parent */
    uint16_t width, height;
    uint16_t border_width;
    uint16_t input_class;
    uint16_t visual_id;
    uint32_t background_pixel;
    uint32_t border_pixel;
    /* The mask the owner of the window selected and the union of every
     * client selection on the window.  Both are kept in step with the
     * selection list by x_window_select_events(). */
    uint32_t event_mask;
    uint32_t all_event_masks;
    struct x_window_selection selections[X_MAX_WINDOW_SELECTIONS];
    int selection_count;
    uint32_t colormap;
    uint32_t cursor;
    uint8_t mapped;
    uint8_t override_redirect;
    uint32_t *backing;
    size_t backing_bytes;
};

struct x_pixmap {
    xid_t id;
    int client;
    uint16_t width, height;
    uint16_t depth;
    uint32_t *pixels;
    size_t bytes;
};

struct x_gc {
    xid_t id;
    int client;
    uint32_t foreground;
    uint32_t background;
    uint32_t function;
    uint32_t plane_mask;
    uint16_t line_width;
    uint8_t fill_style;
    uint8_t subwindow_mode;
    int16_t clip_x, clip_y;
    uint16_t clip_width, clip_height;
    uint8_t graphics_exposures;
    uint16_t dash_offset;
    uint32_t dashes;
    uint32_t font;
};

struct x_atom {
    uint32_t id;
    char name[128];
    size_t name_length;
};

struct x_property {
    xid_t window;
    uint32_t atom;
    uint32_t type;
    uint8_t format;
    uint32_t item_count;
    uint8_t *data;
    size_t bytes;
};

struct x_font {
    xid_t id;
    int client;
    char name[128];
    size_t name_length;
};

struct x_colormap {
    xid_t id;
    int client;
    uint32_t visual_id;
    uint32_t window;
};

struct x_cursor {
    xid_t id;
    int client;
    uint16_t source_x, source_y;
    uint16_t width, height;
    uint16_t hot_x, hot_y;
};

struct x_selection {
    xid_t owner;
    xid_t selection;
};

struct x_client {
    int in_use;
    int fd;
    uint32_t resource_base;
    uint32_t resource_mask;
    uint32_t id_base;
    uint32_t id_mask;
    uint32_t sequence;
    uint8_t byte_order;
    uint8_t big_requests;
    uint8_t close_down;
    /* The sequence number of the reply that was traced last, so that the
     * trace writes one line for a reply and not one for every piece of its
     * data. */
    uint32_t traced_sequence;
    /* The major and minor opcode of the request being processed, so that an
     * error reply can name the request that failed. */
    uint8_t request_major;
    uint8_t request_minor;
    uint8_t *output;
    size_t output_length;
    size_t output_capacity;
};

/* -------------------------------------------------------------- byte order */
static inline uint16_t x_swap16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

static inline uint32_t x_swap32(uint32_t value) {
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}

/* Convert a value into the byte order the client asked for. */
static inline uint16_t client_u16(struct x_client *client, uint16_t value) {
    return client->byte_order == X_MSB_FIRST ? x_swap16(value) : value;
}

static inline uint32_t client_u32(struct x_client *client, uint32_t value) {
    return client->byte_order == X_MSB_FIRST ? x_swap32(value) : value;
}

static inline uint8_t client_u8(struct x_client *client, uint8_t value) {
    (void)client;
    return value;
}

/* A key grab.
 *
 * A window manager grabs the keys it wants to handle itself.  The grab takes
 * the key event away from the window that holds the focus and gives it to
 * the client that asked for the grab. */
struct x_key_grab {
    int client;            /* the client that owns the grab */
    uint32_t owner;        /* the window the grab belongs to */
    uint16_t modifiers;
    uint8_t key;
    uint8_t in_use;
};

struct x_server {
    int listen_fd;
    int width;
    int height;
    uint32_t *screen;
    struct x_window windows[X_MAX_WINDOWS];
    int window_count;
    int stack[X_MAX_WINDOWS];      /* bottom to top */
    struct x_pixmap pixmaps[X_MAX_PIXMAPS];
    struct x_gc gcs[X_MAX_GCS];
    struct x_atom atoms[X_MAX_ATOMS];
    int atom_count;
    struct x_property properties[X_MAX_PROPERTIES];
    struct x_font fonts[X_MAX_FONTS];
    struct x_colormap colormaps[X_MAX_COLORMAPS];
    struct x_cursor cursors[X_MAX_CURSORS];
    struct x_selection selections[X_MAX_SELECTIONS];
    struct x_client clients[X_MAX_CLIENTS];
    struct x_key_grab key_grabs[X_MAX_KEY_GRABS];
    size_t backing_used;
    uint32_t next_atom;
    uint32_t sequence;
    /* The base of the identifiers of the next connection. */
    uint32_t next_resource_base;

    /* Pointer and keyboard state. */
    int16_t pointer_x, pointer_y;
    uint32_t button_mask;
    uint32_t modifier_mask;
    int focus_window;
    int pointer_window;
    uint8_t key_down[32];

    /* The screen rectangle that the next repaint has to refresh. */
    int32_t dirty_x, dirty_y;
    int32_t dirty_width, dirty_height;

    /* Lumen compositor integration. */
    struct akaos_compositor_channel *channel;
    int surface_id;
    uint32_t *surface_pixels;
    size_t surface_bytes;
    int damaged;
    int published;

    /* Persistent keyboard and pointer descriptor. */
    int input_fd;
};

/* --------------------------------------------------------------- accessors */
static inline int client_index_of(struct x_server *server,
                                  struct x_client *client) {
    for (int index = 0; index < X_MAX_CLIENTS; ++index) {
        if (&server->clients[index] == client) return index;
    }
    return -1;
}

static inline struct x_window *x_window_of(struct x_server *server, int index) {
    if (index < 0 || index >= X_MAX_WINDOWS) return NULL;
    if (server->windows[index].id == 0U) return NULL;
    return &server->windows[index];
}

static inline int x_window_index(struct x_server *server, xid_t id) {
    for (int index = 0; index < X_MAX_WINDOWS; ++index) {
        if (server->windows[index].id == id && id != 0U) return index;
    }
    return -1;
}

/* ------------------------------------------------------------------ window */
int x_window_create(struct x_server *server, struct x_client *client,
                    xid_t id, int parent_index, int16_t x, int16_t y,
                    uint16_t width, uint16_t height, uint16_t border_width,
                    uint16_t input_class, uint32_t visual_id);
void x_window_destroy(struct x_server *server, int index);
int x_window_map(struct x_server *server, int index);
void x_window_unmap(struct x_server *server, int index);
void x_window_configure(struct x_server *server, int index, int16_t x, int16_t y,
                        uint16_t width, uint16_t height, uint16_t border_width,
                        uint32_t sibling, uint16_t stack_mode);
void x_window_restack(struct x_server *server, int index, int sibling,
                      uint16_t stack_mode);
void x_window_reparent(struct x_server *server, int index, int new_parent,
                       int16_t x, int16_t y);
void x_server_repaint(struct x_server *server);
void x_server_repaint_rect(struct x_server *server, int16_t x, int16_t y,
                           uint16_t width, uint16_t height);
void x_server_damage(struct x_server *server, int window_index, int16_t x,
                     int16_t y, int16_t width, int16_t height);
void x_window_fill_backing(struct x_server *server, int index, uint32_t pixel);
void x_window_absolute_origin(struct x_server *server, int index,
                              int16_t *out_x, int16_t *out_y);
int x_window_find_at(struct x_server *server, int16_t x, int16_t y);
int x_window_pointer_child(struct x_server *server, int16_t x, int16_t y);

/* --------------------------------------------------------- event selections */
/* Record the event mask that a client selected on a window.
 *
 * The selection replaces the one the client held before, so a client that
 * clears its mask stops receiving the events. */
void x_window_select_events(struct x_server *server, int index, int client_index,
                            uint32_t mask);
/* The mask a single client selected on a window. */
uint32_t x_window_event_mask(struct x_server *server, int index, int client_index);
/* The union of every client selection on a window. */
uint32_t x_window_all_event_masks(struct x_server *server, int index);
/* Drop every selection a client holds, used when a client goes away. */
void x_window_drop_client_selections(struct x_server *server, int client_index);

/* Record and drop a key grab.  A window manager grabs the keys it wants to
 * handle itself. */
void x_server_grab_key(struct x_server *server, int client_index, uint32_t owner,
                       uint16_t modifiers, uint8_t key);
void x_server_ungrab_key(struct x_server *server, int client_index, uint32_t owner,
                         uint16_t modifiers, uint8_t key);

/* Whether a pixmap of that identifier exists.
 *
 * A drawable is either a window or a pixmap, and a request that draws into
 * one has to accept both. */
int x_pixmap_exists(struct x_server *server, uint32_t id);
/* Collect the clients that redirect the children of a window.
 *
 * The window manager selects SubstructureRedirectMask on the root window, so
 * the list has to come from the selections of every client and not from the
 * owner of the window.  Returns the number of clients written. */
int x_window_redirect_clients(struct x_server *server, int index, int *clients,
                              int max_clients);

/* ------------------------------------------------------------- core requests */
void x_core_create_colormap(struct x_server *server, struct x_client *client,
                            uint32_t id, uint32_t window_id, uint32_t visual);
void x_core_free_colormap(struct x_server *server, uint32_t id);
void x_core_alloc_color(struct x_server *server, struct x_client *client,
                        uint32_t colormap_id, uint16_t red, uint16_t green,
                        uint16_t blue);
void x_core_alloc_named_color(struct x_server *server, struct x_client *client,
                              uint32_t colormap_id, const char *name,
                              size_t length);
void x_core_lookup_color(struct x_server *server, struct x_client *client,
                         uint32_t colormap_id, const char *name, size_t length);
void x_core_query_colors(struct x_server *server, struct x_client *client,
                         uint32_t colormap_id, const uint32_t *pixels,
                         size_t count);
void x_core_translate_coordinates(struct x_server *server,
                                  struct x_client *client,
                                  uint32_t source_id, uint32_t destination_id,
                                  int16_t source_x, int16_t source_y);
void x_core_warp_pointer(struct x_server *server, struct x_client *client,
                         uint32_t source_id, int16_t source_x, int16_t source_y,
                         uint32_t destination_id, int16_t destination_x,
                         int16_t destination_y);
void x_core_grab_pointer(struct x_server *server, struct x_client *client);
void x_core_grab_keyboard(struct x_server *server, struct x_client *client);
void x_core_open_font(struct x_server *server, struct x_client *client,
                      uint32_t id, const char *name, size_t length);
void x_core_close_font(struct x_server *server, uint32_t id);
void x_core_list_fonts(struct x_server *server, struct x_client *client);
void x_core_get_font_path(struct x_server *server, struct x_client *client);
void x_core_convert_selection(struct x_server *server, struct x_client *client,
                              uint32_t requestor, uint32_t selection,
                              uint32_t target, uint32_t property,
                              uint32_t time);

/* ------------------------------------------------------------------ events */
void x_event_queue(struct x_client *client, const void *payload, size_t length,
                   uint32_t event_window);
void x_event_simple(struct x_server *server, uint8_t event_code,
                    int window_index, int32_t x, int32_t y);
void x_event_configure(struct x_server *server, int index);
void x_event_map(struct x_server *server, int index);
void x_event_unmap(struct x_server *server, int index);
void x_event_create(struct x_server *server, int index);
void x_event_destroy(struct x_server *server, int index);
void x_event_reparent(struct x_server *server, int index, int old_parent);
void x_event_property(struct x_server *server, int index, uint32_t atom,
                      uint8_t state);
void x_event_expose(struct x_server *server, int index, int16_t x, int16_t y,
                    uint16_t width, uint16_t height);
void x_event_focus(struct x_server *server, int index, int mode);
void x_event_enter(struct x_server *server, int index, int16_t x, int16_t y);
void x_event_leave(struct x_server *server, int index, int16_t x, int16_t y);
void x_event_key(struct x_server *server, uint8_t press, uint8_t keycode);
void x_event_button(struct x_server *server, uint8_t press, uint8_t button,
                    int16_t x, int16_t y);
void x_event_motion(struct x_server *server, int16_t x, int16_t y);
void x_event_client_message(struct x_server *server, xid_t window,
                            uint32_t message_type, const uint32_t *data);

/* Tell the clients that redirect the parent of a window that the window
 * wants to be mapped or configured.
 *
 * Both return 1 when the request was redirected and the window must not be
 * changed yet, and 0 when the request has to be performed directly. */
int x_event_map_request(struct x_server *server, int index, int requester);
int x_event_configure_request(struct x_server *server, struct x_client *requester,
                              int index, uint32_t value_mask, int16_t x, int16_t y,
                              uint16_t width, uint16_t height,
                              uint16_t border_width, uint32_t sibling,
                              uint16_t stack_mode);

/* ----------------------------------------------------------------- replies */
void x_reply_error(struct x_client *client, uint8_t error_code, uint8_t minor,
                   uint32_t resource);
void x_reply_simple(struct x_client *client, uint8_t detail);
void x_reply_bytes(struct x_client *client, const void *payload, size_t length);

/* ------------------------------------------------------------------ output */
void x_client_flush(struct x_client *client);
int x_client_write(struct x_client *client, const void *data, size_t length);
void x_client_queue(struct x_client *client, const void *data, size_t length);
void x_client_accept(struct x_server *server, int fd);
void x_client_service(struct x_server *server, struct x_client *client);
void x_client_disconnect(struct x_server *server, struct x_client *client);
void x_server_flush_clients(struct x_server *server);
void x_server_publish_damage(struct x_server *server);

/* ----------------------------------------------------------------- drawing */
uint32_t *x_drawable_pixels(struct x_server *server, xid_t drawable,
                            uint16_t *width, uint16_t *height, int *is_window);
void x_drawable_put_pixel(struct x_server *server, xid_t drawable, int16_t x,
                          int16_t y, uint32_t pixel);
void x_drawable_fill(struct x_server *server, xid_t drawable, int16_t x,
                     int16_t y, uint16_t width, uint16_t height, uint32_t pixel);
void x_drawable_copy(struct x_server *server, xid_t source, int16_t src_x,
                     int16_t src_y, xid_t destination, int16_t dst_x,
                     int16_t dst_y, uint16_t width, uint16_t height);
void x_drawable_image(struct x_server *server, xid_t drawable, int16_t x,
                      int16_t y, uint16_t width, uint16_t height,
                      uint16_t depth, uint16_t format, const void *pixels);
void x_drawable_rectangle(struct x_server *server, xid_t drawable, int16_t x,
                          int16_t y, uint16_t width, uint16_t height,
                          uint32_t pixel);
void x_drawable_line(struct x_server *server, xid_t drawable, int16_t x0,
                     int16_t y0, int16_t x1, int16_t y1, uint32_t pixel);

/* --------------------------------------------------------------- resources */
uint32_t x_atom_intern(struct x_server *server, const char *name, size_t length,
                       int only_if_exists);
const char *x_atom_name(struct x_server *server, uint32_t atom,
                         size_t *length);
int x_property_set(struct x_server *server, xid_t window, uint32_t atom,
                   uint32_t type, uint8_t format, const void *data,
                   uint32_t item_count);
int x_property_get(struct x_server *server, xid_t window, uint32_t atom,
                   uint32_t *type, uint8_t *format, uint32_t *item_count,
                   const uint8_t **data);
int x_property_delete(struct x_server *server, xid_t window, uint32_t atom);
int x_property_list(struct x_server *server, xid_t window, uint32_t *atoms,
                    uint32_t max_count);

/* ------------------------------------------------------------------ colours */
void x_rgb_to_pixel(uint16_t red, uint16_t green, uint16_t blue,
                    uint32_t *pixel);

/* ------------------------------------------------------------------- input */
void x_input_pump(struct x_server *server);
void x_input_handle_key(struct x_server *server, uint16_t code, uint8_t value);
void x_input_handle_button(struct x_server *server, uint16_t code, uint8_t value);
void x_input_handle_motion(struct x_server *server, int32_t dx, int32_t dy);
void x_input_handle_absolute(struct x_server *server, int32_t x, int32_t y);
uint32_t x_keysym_for_keycode(struct x_server *server, uint32_t xcode);

/* ------------------------------------------------------------------ atoms */
void x_server_install_atoms(struct x_server *server);

#endif
