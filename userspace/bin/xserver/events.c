/* ------------------------------------------------------------------- events
 *
 * Every X11 event is a thirty two byte record.  The first four bytes are the
 * event code, the detail byte and the sequence number of the last request the
 * client sent.  The remaining fields differ per event, so each event is built
 * with its own layout instead of a shared template.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

/* Deliver an event to every client that asked for it.
 *
 * The event selection belongs to a client and a window, so the search walks
 * the whole ancestor chain and hands the event to every client that selected
 * the kind somewhere along the way.  A client that selected the kind on
 * several windows of the chain still receives the event once.
 *
 * A kind of zero means that the event always belongs to the window itself,
 * so it goes to the owner of the window and to nobody else. */
static void deliver_event(struct x_server *server, int window_index,
                          uint32_t mask, const void *payload, size_t length) {
    if (window_index < 0) return;
    if (mask == 0U) {
        struct x_window *window = &server->windows[window_index];
        if (window->client >= 0) {
            x_event_queue(&server->clients[window->client], payload, length,
                          window->id);
        }
        return;
    }
    uint32_t delivered = 0U;
    int index = window_index;
    while (index >= 0) {
        for (int slot = 0; slot < X_MAX_CLIENTS; ++slot) {
            if ((delivered & (1U << slot)) != 0U) continue;
            if (!server->clients[slot].in_use) continue;
            if ((x_window_event_mask(server, index, slot) & mask) == 0U) continue;
            delivered |= 1U << slot;
            x_event_queue(&server->clients[slot], payload, length,
                          server->windows[index].id);
        }
        index = server->windows[index].parent;
    }
}

/* Deliver an event to the clients that selected the kind on this window.
 *
 * The events a parent reports about its children belong to the parent alone,
 * so they do not travel up the chain. */
static void deliver_event_window(struct x_server *server, int window_index,
                                 uint32_t mask, const void *payload,
                                 size_t length) {
    if (window_index < 0 || mask == 0U) return;
    for (int slot = 0; slot < X_MAX_CLIENTS; ++slot) {
        if (!server->clients[slot].in_use) continue;
        if ((x_window_event_mask(server, window_index, slot) & mask) == 0U) {
            continue;
        }
        x_event_queue(&server->clients[slot], payload, length,
                      server->windows[window_index].id);
    }
}

/* Whether the first window of the event record is the window the event is
 * reported on.  The keyboard, button and motion events carry a timestamp in
 * that place instead, and the mapping notifications carry no window at all. */
static int event_reports_window(uint8_t event_code) {
    switch (event_code & 0x7fU) {
    case KeyPress:
    case KeyRelease:
    case ButtonPress:
    case ButtonRelease:
    case MotionNotify:
    case EnterNotify:
    case LeaveNotify:
    case SelectionClear:
    case SelectionRequest:
    case SelectionNotify:
    case MappingNotify:
    case KeymapNotify:
        return 0;
    default:
        return 1;
    }
}

void x_event_queue(struct x_client *client, const void *payload, size_t length,
                   uint32_t event_window) {
    if (!client || !client->in_use) return;
    /* The protocol gives every event the sequence number of the last request
     * the server processed from the client that receives the event.  An event
     * can be built for one window and delivered to a client that owns another
     * window, so the sequence number belongs to the receiver and not to the
     * window.  XCB advances its own request-completion tracking from this
     * field; a zero or a stale value makes it complete requests whose replies
     * have not been read yet, and the reply of those requests is then lost.
     *
     * The first window of the record has the same per receiver meaning: the
     * window a delivery talks about is the window of that delivery, which is
     * the window itself for a selection on the window and the parent for the
     * selection on the children.  A window manager decides from that field
     * whether an event belongs to the window it manages. */
    uint8_t stamped[32];
    if (length == sizeof(stamped)) {
        memcpy(stamped, payload, sizeof(stamped));
        uint16_t sequence = client_u16(client, client->sequence);
        memcpy(stamped + 2, &sequence, sizeof(sequence));
        if (event_window != 0U && event_reports_window(stamped[0])) {
            uint32_t window = client_u32(client, event_window);
            memcpy(stamped + 4, &window, sizeof(window));
        }
        payload = stamped;
    }
    if (server_trace_enabled) {
        const uint8_t *bytes = payload;
        printf("xserver: t%llu c%d event %u seq %u\n",
               (unsigned long long)server_now_millis(), client->fd,
               (unsigned)bytes[0], (unsigned)(bytes[2] | (bytes[3] << 8)));
        fflush(stdout);
    }
    x_client_queue(client, payload, length);
}

/* The sequence number of the last request the owning client sent. */
static uint16_t event_sequence(struct x_server *server, int window_index) {
    if (window_index < 0) return 0U;
    int client = server->windows[window_index].client;
    if (client < 0) return 0U;
    return (uint16_t)server->clients[client].sequence;
}

static void event_header(uint8_t *buffer, uint8_t event_code, uint8_t detail,
                         uint16_t sequence) {
    memset(buffer, 0, 32);
    buffer[0] = event_code;
    buffer[1] = detail;
    memcpy(buffer + 2, &sequence, 2);
}

static void put_u16(uint8_t *buffer, size_t offset, uint16_t value) {
    memcpy(buffer + offset, &value, 2);
}

static void put_u32(uint8_t *buffer, size_t offset, uint32_t value) {
    memcpy(buffer + offset, &value, 4);
}

/* Write a value in the byte order of the client that receives the event.
 *
 * The redirect events go to a window manager that did not ask for the
 * request, so their fields follow the order of the client that made it. */
static void put_u16_client(struct x_client *client, uint8_t *buffer,
                           size_t offset, uint16_t value) {
    uint16_t converted = client ? client_u16(client, value) : value;
    memcpy(buffer + offset, &converted, 2);
}

void x_event_simple(struct x_server *server, uint8_t event_code,
                    int window_index, int32_t x, int32_t y) {
    (void)x;
    (void)y;
    uint8_t buffer[32];
    event_header(buffer, event_code, 0U, event_sequence(server, window_index));
    if (window_index >= 0) {
        put_u32(buffer, 4, server->windows[window_index].id);
    }
    deliver_event(server, window_index, 0U, buffer, sizeof(buffer));
}

void x_event_map(struct x_server *server, int index) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, MapNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    put_u32(buffer, 8, window->id);
    buffer[12] = window->override_redirect ? 1U : 0U;
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    /* A window manager watches the children of the root window, so it wants
     * to hear about every window that appears. */
    deliver_event_window(server, window->parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

void x_event_unmap(struct x_server *server, int index) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, UnmapNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    put_u32(buffer, 8, window->id);
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    deliver_event_window(server, window->parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

void x_event_create(struct x_server *server, int index) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint32_t parent = window->parent >= 0 ? server->windows[window->parent].id : 0U;
    uint8_t buffer[32];
    event_header(buffer, CreateNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, parent);
    put_u32(buffer, 8, window->id);
    put_u16(buffer, 12, (uint16_t)window->x);
    put_u16(buffer, 14, (uint16_t)window->y);
    put_u16(buffer, 16, window->width);
    put_u16(buffer, 18, window->height);
    put_u16(buffer, 20, window->border_width);
    buffer[22] = window->override_redirect ? 1U : 0U;
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    deliver_event_window(server, window->parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

void x_event_destroy(struct x_server *server, int index) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, DestroyNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    put_u32(buffer, 8, window->id);
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    deliver_event_window(server, window->parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

void x_event_reparent(struct x_server *server, int index, int old_parent) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    int new_parent = window->parent;
    uint32_t parent_id = new_parent >= 0 ? server->windows[new_parent].id : 0U;
    uint8_t buffer[32];
    event_header(buffer, ReparentNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, parent_id);
    put_u32(buffer, 8, window->id);
    put_u32(buffer, 12, parent_id);
    put_u16(buffer, 16, (uint16_t)window->x);
    put_u16(buffer, 18, (uint16_t)window->y);
    buffer[20] = window->override_redirect ? 1U : 0U;
    /* Both parents report the move, and so does the window itself. */
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    deliver_event_window(server, old_parent, StructureNotifyMask, buffer,
                         sizeof(buffer));
    deliver_event_window(server, old_parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
    deliver_event_window(server, new_parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

void x_event_configure(struct x_server *server, int index) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    /* The origin of the window is relative to the window the event is
     * reported for, which is the parent of the window. */
    uint32_t parent_id = window->parent >= 0 ? server->windows[window->parent].id : 0U;
    uint8_t buffer[32];
    event_header(buffer, ConfigureNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, parent_id);
    put_u32(buffer, 8, window->id);
    put_u32(buffer, 12, 0U);
    put_u16(buffer, 16, (uint16_t)window->x);
    put_u16(buffer, 18, (uint16_t)window->y);
    put_u16(buffer, 20, window->width);
    put_u16(buffer, 22, window->height);
    put_u16(buffer, 24, window->border_width);
    buffer[26] = window->override_redirect ? 1U : 0U;
    deliver_event(server, index, StructureNotifyMask, buffer, sizeof(buffer));
    deliver_event_window(server, window->parent, SubstructureNotifyMask, buffer,
                         sizeof(buffer));
}

/* ------------------------------------------------------------------ redirect */
/* Tell the window manager that a window wants to be mapped.
 *
 * The request goes to every client that redirects the children of the parent
 * window.  A client that redirects the window itself is left out, because it
 * manages that window directly, and the window owner does not get the request
 * either once another client redirects it. */
int x_event_map_request(struct x_server *server, int index, int requester) {
    if (index < 0 || index >= X_MAX_WINDOWS) return 0;
    struct x_window *window = &server->windows[index];
    if (window->id == 0U) return 0;
    /* A window that asks to bypass the window manager is never redirected. */
    if (window->override_redirect) return 0;
    int parent = window->parent;
    if (parent < 0) return 0;

    int redirecting[X_MAX_CLIENTS];
    int count = x_window_redirect_clients(server, parent, redirecting,
                                          X_MAX_CLIENTS);
    if (count == 0) return 0;
    /* The client that asked for the redirect performs its own requests, so
     * the window manager can map the window it was told about. */
    for (int position = 0; position < count; ++position) {
        if (redirecting[position] == requester) return 0;
    }

    uint32_t parent_id = server->windows[parent].id;
    int sent = 0;
    for (int position = 0; position < count; ++position) {
        int client_index = redirecting[position];
        if ((x_window_event_mask(server, index, client_index) &
             SubstructureRedirectMask) != 0U) {
            continue;
        }
        uint8_t buffer[32];
        event_header(buffer, MapRequest, 0U,
                     (uint16_t)server->clients[client_index].sequence);
        put_u32(buffer, 4, parent_id);
        put_u32(buffer, 8, window->id);
        buffer[12] = 1U;    /* the window wants to be mapped */
        x_event_queue(&server->clients[client_index], buffer, sizeof(buffer),
                      parent_id);
        sent = 1;
    }
    return sent;
}

/* Tell the window manager that a window wants to be configured.
 *
 * The values that the request asked for travel in the event in the order of
 * the request, and the value mask says which of them are set. */
int x_event_configure_request(struct x_server *server, struct x_client *requester,
                              int index, uint32_t value_mask, int16_t x, int16_t y,
                              uint16_t width, uint16_t height,
                              uint16_t border_width, uint32_t sibling,
                              uint16_t stack_mode) {
    if (index < 0 || index >= X_MAX_WINDOWS) return 0;
    struct x_window *window = &server->windows[index];
    if (window->id == 0U) return 0;
    if (window->override_redirect) return 0;
    int parent = window->parent;
    if (parent < 0) return 0;

    int redirecting[X_MAX_CLIENTS];
    int count = x_window_redirect_clients(server, parent, redirecting,
                                          X_MAX_CLIENTS);
    if (count == 0) return 0;
    int requester_index = client_index_of(server, requester);
    for (int position = 0; position < count; ++position) {
        if (redirecting[position] == requester_index) return 0;
    }

    uint32_t parent_id = server->windows[parent].id;
    int sent = 0;
    for (int position = 0; position < count; ++position) {
        int client_index = redirecting[position];
        if ((x_window_event_mask(server, index, client_index) &
             SubstructureRedirectMask) != 0U) {
            continue;
        }
        /* The event carries the stack mode in the detail byte, the parent of
         * the window, the window itself, the sibling above it and the values
         * the request asked for.  The values of the event are narrow, even
         * though the values of the request are full words. */
        uint8_t buffer[32];
        event_header(buffer, ConfigureRequest, (uint8_t)stack_mode,
                     (uint16_t)server->clients[client_index].sequence);
        put_u32(buffer, 4, parent_id);
        put_u32(buffer, 8, window->id);
        put_u32(buffer, 12, sibling);
        put_u16_client(requester, buffer, 16, (uint16_t)x);
        put_u16_client(requester, buffer, 18, (uint16_t)y);
        put_u16_client(requester, buffer, 20, width);
        put_u16_client(requester, buffer, 22, height);
        put_u16_client(requester, buffer, 24, border_width);
        put_u16_client(requester, buffer, 26, (uint16_t)value_mask);
        x_event_queue(&server->clients[client_index], buffer, sizeof(buffer),
                      parent_id);
        sent = 1;
    }
    return sent;
}

/* The time of an event.
 *
 * The protocol gives every event a time in milliseconds, and a client that
 * looks for an event with a time of its own finds nothing when the time is
 * zero.  The display server counts from the moment it started, so that the
 * times of two events can be compared. */
static uint32_t event_time(void) {
    uint64_t now = server_now_millis();
    return (uint32_t)(now & 0xffffffffU);
}

void x_event_property(struct x_server *server, int index, uint32_t atom,
                      uint8_t state) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, PropertyNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    put_u32(buffer, 8, atom);
    put_u32(buffer, 12, event_time());
    buffer[16] = state;
    if (server_trace_enabled) {
        printf("xserver: property notify on window 0x%x mask 0x%x\n",
               (unsigned)window->id,
               (unsigned)x_window_event_mask(server, index, window->client));
        fflush(stdout);
    }
    deliver_event(server, index, PropertyChangeMask, buffer, sizeof(buffer));
}

void x_event_expose(struct x_server *server, int index, int16_t x, int16_t y,
                    uint16_t width, uint16_t height) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, Expose, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    put_u16(buffer, 8, (uint16_t)x);
    put_u16(buffer, 10, (uint16_t)y);
    put_u16(buffer, 12, width);
    put_u16(buffer, 14, height);
    put_u16(buffer, 16, 0U);
    deliver_event(server, index, ExposureMask, buffer, sizeof(buffer));
}

void x_event_focus(struct x_server *server, int index, int mode) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, FocusIn, 0U, event_sequence(server, index));
    put_u32(buffer, 4, window->id);
    buffer[8] = (uint8_t)mode;
    deliver_event(server, index, FocusChangeMask, buffer, sizeof(buffer));
}

void x_event_enter(struct x_server *server, int index, int16_t x, int16_t y) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, EnterNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, event_time());
    put_u32(buffer, 8, server->windows[0].id);
    put_u32(buffer, 12, window->id);
    put_u32(buffer, 16, 0U);
    put_u16(buffer, 20, (uint16_t)x);
    put_u16(buffer, 22, (uint16_t)y);
    put_u16(buffer, 24, (uint16_t)x);
    put_u16(buffer, 26, (uint16_t)y);
    buffer[30] = 2U;
    deliver_event(server, index, EnterWindowMask, buffer, sizeof(buffer));
}

void x_event_leave(struct x_server *server, int index, int16_t x, int16_t y) {
    if (index < 0) return;
    struct x_window *window = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, LeaveNotify, 0U, event_sequence(server, index));
    put_u32(buffer, 4, event_time());
    put_u32(buffer, 8, server->windows[0].id);
    put_u32(buffer, 12, window->id);
    put_u32(buffer, 16, 0U);
    put_u16(buffer, 20, (uint16_t)x);
    put_u16(buffer, 22, (uint16_t)y);
    put_u16(buffer, 24, (uint16_t)x);
    put_u16(buffer, 26, (uint16_t)y);
    buffer[30] = 2U;
    deliver_event(server, index, LeaveWindowMask, buffer, sizeof(buffer));
}

/* The pointer and keyboard events share one layout.
 *
 * The record is returned to the caller, because a key that a client grabbed
 * has to reach that client instead of the window that owns the focus. */
static void build_pointer_event(struct x_server *server, uint8_t *buffer,
                                uint8_t event_code, uint8_t detail,
                                int window_index, int16_t x, int16_t y) {
    struct x_window *window = window_index >= 0 ? &server->windows[window_index] : NULL;
    event_header(buffer, event_code, detail, event_sequence(server, window_index));
    put_u32(buffer, 4, event_time());
    put_u32(buffer, 8, server->windows[0].id);
    put_u32(buffer, 12, window ? window->id : 0U);
    put_u32(buffer, 16, 0U);
    put_u16(buffer, 20, (uint16_t)x);
    put_u16(buffer, 22, (uint16_t)y);
    /* The event coordinates are relative to the event window while the root
     * coordinates stay in screen space. */
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    if (window) {
        x_window_absolute_origin(server, window_index, &origin_x, &origin_y);
    }
    put_u16(buffer, 24, (uint16_t)(int16_t)(x - origin_x));
    put_u16(buffer, 26, (uint16_t)(int16_t)(y - origin_y));
    put_u16(buffer, 28, (uint16_t)server->modifier_mask);
    buffer[30] = 1U;
}

static uint32_t pointer_event_mask(uint8_t event_code) {
    if (event_code == KeyPress) return KeyPressMask;
    if (event_code == KeyRelease) return KeyReleaseMask;
    if (event_code == ButtonPress) return ButtonPressMask;
    if (event_code == ButtonRelease) return ButtonReleaseMask;
    return PointerMotionMask;
}

void x_event_key(struct x_server *server, uint8_t press, uint8_t keycode) {
    uint8_t event_code = press ? KeyPress : KeyRelease;
    /* A key grab takes the key event away from the window that holds the
     * focus and gives it to the client that asked for the grab. */
    for (int grab = 0; grab < X_MAX_KEY_GRABS; ++grab) {
        struct x_key_grab *entry = &server->key_grabs[grab];
        if (!entry->in_use) continue;
        if (entry->key != keycode) continue;
        if ((server->modifier_mask & entry->modifiers) != entry->modifiers) continue;
        int slot = entry->client;
        if (slot < 0 || slot >= X_MAX_CLIENTS) continue;
        if (!server->clients[slot].in_use) continue;
        uint8_t buffer[32];
        build_pointer_event(server, buffer, event_code, keycode,
                            server->focus_window, server->pointer_x,
                            server->pointer_y);
        put_u32(buffer, 16, entry->owner);
        x_event_queue(&server->clients[slot], buffer, sizeof(buffer), 0U);
        return;
    }
    /* Key events belong to the focus window. */
    int index = server->focus_window;
    if (index < 0) index = server->pointer_window;
    if (index < 0) return;
    uint8_t buffer[32];
    build_pointer_event(server, buffer, event_code, keycode, index,
                        server->pointer_x, server->pointer_y);
    deliver_event(server, index, pointer_event_mask(event_code), buffer,
                  sizeof(buffer));
}

void x_server_grab_key(struct x_server *server, int client_index, uint32_t owner,
                       uint16_t modifiers, uint8_t key) {
    if (client_index < 0 || client_index >= X_MAX_CLIENTS) return;
    for (int grab = 0; grab < X_MAX_KEY_GRABS; ++grab) {
        struct x_key_grab *entry = &server->key_grabs[grab];
        if (entry->in_use) continue;
        entry->client = client_index;
        entry->owner = owner;
        entry->modifiers = modifiers;
        entry->key = key;
        entry->in_use = 1U;
        return;
    }
}

void x_server_ungrab_key(struct x_server *server, int client_index, uint32_t owner,
                         uint16_t modifiers, uint8_t key) {
    for (int grab = 0; grab < X_MAX_KEY_GRABS; ++grab) {
        struct x_key_grab *entry = &server->key_grabs[grab];
        if (!entry->in_use) continue;
        if (entry->client != client_index) continue;
        if (entry->owner != owner) continue;
        if (entry->modifiers != modifiers) continue;
        if (entry->key != key) continue;
        entry->in_use = 0U;
        return;
    }
}

void x_event_button(struct x_server *server, uint8_t press, uint8_t button,
                    int16_t x, int16_t y) {
    int index = server->pointer_window;
    if (index < 0) index = server->focus_window;
    if (index < 0) return;
    printf("xserver: button %u %s at %d,%d on window 0x%x\n", (unsigned)button,
           press ? "press" : "release", (int)x, (int)y,
           (unsigned)server->windows[index].id);
    if (server_trace_enabled) {
        /* The tree of the windows tells where the windows of the clients
         * are, and which of them is mapped. */
        for (int window = 0; window < X_MAX_WINDOWS; ++window) {
            struct x_window *entry = &server->windows[window];
            if (entry->id == 0U) continue;
            int16_t origin_x = 0;
            int16_t origin_y = 0;
            x_window_absolute_origin(server, window, &origin_x, &origin_y);
            printf("xserver:   window 0x%x at %d,%d size %ux%u mapped %u "
                   "parent %d\n", (unsigned)entry->id, (int)origin_x,
                   (int)origin_y, (unsigned)entry->width, (unsigned)entry->height,
                   (unsigned)entry->mapped, entry->parent);
        }
        fflush(stdout);
    }
    fflush(stdout);
    uint8_t buffer[32];
    build_pointer_event(server, buffer, press ? ButtonPress : ButtonRelease,
                        button, index, x, y);
    deliver_event(server, index, pointer_event_mask(press ? ButtonPress :
                                                    ButtonRelease),
                  buffer, sizeof(buffer));
}

void x_event_motion(struct x_server *server, int16_t x, int16_t y) {
    int index = server->pointer_window;
    if (index < 0) return;
    uint8_t buffer[32];
    build_pointer_event(server, buffer, MotionNotify, 0U, index, x, y);
    deliver_event(server, index, PointerMotionMask, buffer, sizeof(buffer));
}

void x_event_client_message(struct x_server *server, xid_t window,
                            uint32_t message_type, const uint32_t *data) {
    int index = x_window_index(server, window);
    if (index < 0) return;
    struct x_window *owner = &server->windows[index];
    uint8_t buffer[32];
    event_header(buffer, ClientMessage, 0U, event_sequence(server, index));
    put_u32(buffer, 4, owner->id);
    put_u32(buffer, 8, message_type);
    for (size_t index2 = 0U; index2 < 5U; ++index2) {
        put_u32(buffer, 12 + index2 * 4U, data ? data[index2] : 0U);
    }
    deliver_event(server, index, 0U, buffer, sizeof(buffer));
}
