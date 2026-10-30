/* Window tree management and screen repaint for the akaOS X11 display server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

/* --------------------------------------------------------------- backing store */
static uint32_t *allocate_backing(struct x_server *server, uint16_t width,
                                  uint16_t height) {
    if (width == 0U || height == 0U) return NULL;
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (bytes == 0U || server->backing_used + bytes > X_BACKING_POOL_BYTES) {
        return NULL;
    }
    uint32_t *pixels = calloc(1U, bytes);
    if (!pixels) return NULL;
    server->backing_used += bytes;
    return pixels;
}

static void release_backing(struct x_server *server, struct x_window *window) {
    if (!window->backing) return;
    free(window->backing);
    server->backing_used -= window->backing_bytes;
    window->backing = NULL;
    window->backing_bytes = 0U;
}

/* --------------------------------------------------------- event selections */
int x_pixmap_exists(struct x_server *server, uint32_t id) {
    if (id == 0U) return 0;
    for (int index = 0; index < X_MAX_PIXMAPS; ++index) {
        if (server->pixmaps[index].id == id) return 1;
    }
    return 0;
}

void x_window_select_events(struct x_server *server, int index, int client_index,
                            uint32_t mask) {
    if (index < 0 || index >= X_MAX_WINDOWS) return;
    if (client_index < 0 || client_index >= X_MAX_CLIENTS) return;
    struct x_window *window = &server->windows[index];
    if (window->id == 0U) return;

    int slot = -1;
    for (int position = 0; position < window->selection_count; ++position) {
        if (window->selections[position].client == client_index) {
            slot = position;
            break;
        }
    }
    if (slot < 0) {
        if (window->selection_count >= X_MAX_WINDOW_SELECTIONS) return;
        slot = window->selection_count++;
        window->selections[slot].client = client_index;
    }
    window->selections[slot].mask = mask;

    /* The owner mask mirrors the selection of the owner so that a window
     * still knows the mask of its own client. */
    if (window->client == client_index) window->event_mask = mask;
    window->all_event_masks = x_window_all_event_masks(server, index);
}

uint32_t x_window_event_mask(struct x_server *server, int index, int client_index) {
    if (index < 0 || index >= X_MAX_WINDOWS) return 0U;
    struct x_window *window = &server->windows[index];
    for (int position = 0; position < window->selection_count; ++position) {
        if (window->selections[position].client == client_index) {
            return window->selections[position].mask;
        }
    }
    /* A client that has not recorded a selection yet still owns the window
     * and keeps the mask the window carries for its owner. */
    if (client_index >= 0 && client_index == window->client) {
        return window->event_mask;
    }
    return 0U;
}

uint32_t x_window_all_event_masks(struct x_server *server, int index) {
    if (index < 0 || index >= X_MAX_WINDOWS) return 0U;
    struct x_window *window = &server->windows[index];
    uint32_t mask = 0U;
    for (int position = 0; position < window->selection_count; ++position) {
        mask |= window->selections[position].mask;
    }
    if (window->client >= 0) mask |= window->event_mask;
    return mask;
}

void x_window_drop_client_selections(struct x_server *server, int client_index) {
    if (client_index < 0) return;
    for (int index = 0; index < X_MAX_WINDOWS; ++index) {
        struct x_window *window = &server->windows[index];
        if (window->id == 0U) continue;
        int position = 0;
        while (position < window->selection_count) {
            if (window->selections[position].client == client_index) {
                for (int move = position; move + 1 < window->selection_count;
                     ++move) {
                    window->selections[move] = window->selections[move + 1];
                }
                --window->selection_count;
                continue;
            }
            ++position;
        }
        if (window->client == client_index) window->event_mask = 0U;
        window->all_event_masks = x_window_all_event_masks(server, index);
    }
}

int x_window_redirect_clients(struct x_server *server, int index, int *clients,
                              int max_clients) {
    if (index < 0 || index >= X_MAX_WINDOWS) return 0;
    int count = 0;
    for (int slot = 0; slot < X_MAX_CLIENTS && count < max_clients; ++slot) {
        if (!server->clients[slot].in_use) continue;
        if ((x_window_event_mask(server, index, slot) & SubstructureRedirectMask)
            == 0U) {
            continue;
        }
        clients[count++] = slot;
    }
    return count;
}

/* ------------------------------------------------------------------ window */
int x_window_create(struct x_server *server, struct x_client *client,
                    xid_t id, int parent_index, int16_t x, int16_t y,
                    uint16_t width, uint16_t height, uint16_t border_width,
                    uint16_t input_class, uint32_t visual_id) {
    int index = -1;
    for (int candidate = 0; candidate < X_MAX_WINDOWS; ++candidate) {
        if (server->windows[candidate].id == 0U) { index = candidate; break; }
    }
    if (index < 0) return -1;

    struct x_window *window = &server->windows[index];
    memset(window, 0, sizeof(*window));
    window->id = id;
    window->client = client ? (int)(client - server->clients) : -1;
    window->parent = parent_index;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->border_width = border_width;
    window->input_class = input_class;
    window->visual_id = visual_id;
    window->background_pixel = 0U;
    window->border_pixel = 0U;
    window->colormap = 0U;
    window->first_child = -1;
    window->next_sibling = -1;
    window->prev_sibling = -1;

    if (width && height) {
        window->backing = allocate_backing(server, width, height);
        window->backing_bytes = (size_t)width * (size_t)height *
                                sizeof(uint32_t);
    }

    /* Append to the parent's child list. */
    if (parent_index >= 0) {
        struct x_window *parent = &server->windows[parent_index];
        if (parent->first_child < 0) {
            parent->first_child = index;
        } else {
            int last = parent->first_child;
            while (server->windows[last].next_sibling >= 0) {
                last = server->windows[last].next_sibling;
            }
            server->windows[last].next_sibling = index;
            window->prev_sibling = last;
        }
    }
    ++server->window_count;
    return index;
}

static void unlink_from_parent(struct x_server *server, int index) {
    struct x_window *window = &server->windows[index];
    if (window->parent < 0) return;
    struct x_window *parent = &server->windows[window->parent];
    if (window->prev_sibling >= 0) {
        server->windows[window->prev_sibling].next_sibling = window->next_sibling;
    } else if (parent->first_child == index) {
        parent->first_child = window->next_sibling;
    }
    if (window->next_sibling >= 0) {
        server->windows[window->next_sibling].prev_sibling = window->prev_sibling;
    }
    window->parent = -1;
    window->prev_sibling = -1;
    window->next_sibling = -1;
}

void x_window_destroy(struct x_server *server, int index) {
    struct x_window *window = &server->windows[index];
    if (window->id == 0U) return;

    /* Destroy the subtree first. */
    while (window->first_child >= 0) {
        x_window_destroy(server, window->first_child);
    }
    x_event_destroy(server, index);

    /* Remove from the stacking order. */
    for (int position = 0; position < X_MAX_WINDOWS; ++position) {
        if (server->stack[position] != index) continue;
        for (int move = position; move + 1 < X_MAX_WINDOWS; ++move) {
            server->stack[move] = server->stack[move + 1];
        }
        server->stack[X_MAX_WINDOWS - 1] = -1;
        break;
    }

    unlink_from_parent(server, index);
    release_backing(server, window);
    memset(window, 0, sizeof(*window));
    window->first_child = -1;
    window->next_sibling = -1;
    window->prev_sibling = -1;
    window->parent = -1;
    if (server->window_count > 0) --server->window_count;
}

static void restack_child(struct x_server *server, int index, int sibling,
                           uint16_t stack_mode) {
    struct x_window *window = &server->windows[index];
    int parent = window->parent;
    if (parent < 0 || parent >= X_MAX_WINDOWS) return;
    if (sibling == index || sibling < 0 || sibling >= X_MAX_WINDOWS ||
        server->windows[sibling].parent != parent) sibling = -1;

    unlink_from_parent(server, index);
    window->parent = parent;
    if (sibling >= 0) {
        struct x_window *relative = &server->windows[sibling];
        if (stack_mode == 0U) {
            window->prev_sibling = sibling;
            window->next_sibling = relative->next_sibling;
            if (relative->next_sibling >= 0)
                server->windows[relative->next_sibling].prev_sibling = index;
            relative->next_sibling = index;
        } else {
            window->next_sibling = sibling;
            window->prev_sibling = relative->prev_sibling;
            if (relative->prev_sibling >= 0)
                server->windows[relative->prev_sibling].next_sibling = index;
            else
                server->windows[parent].first_child = index;
            relative->prev_sibling = index;
        }
    } else if (stack_mode == 0U) {
        int last = server->windows[parent].first_child;
        if (last < 0) {
            server->windows[parent].first_child = index;
        } else {
            while (server->windows[last].next_sibling >= 0)
                last = server->windows[last].next_sibling;
            server->windows[last].next_sibling = index;
            window->prev_sibling = last;
        }
    } else {
        int first = server->windows[parent].first_child;
        window->next_sibling = first;
        if (first >= 0) server->windows[first].prev_sibling = index;
        server->windows[parent].first_child = index;
    }
}

static void stack_insert(struct x_server *server, int index, int sibling,
                         uint16_t stack_mode) {
    for (int position = 0; position < X_MAX_WINDOWS; ++position) {
        if (server->stack[position] != index) continue;
        for (int move = position; move + 1 < X_MAX_WINDOWS; ++move)
            server->stack[move] = server->stack[move + 1];
        server->stack[X_MAX_WINDOWS - 1] = -1;
        break;
    }

    int target = -1;
    int sibling_position = -1;
    if (sibling >= 0 && sibling != index) {
        for (int position = 0; position < X_MAX_WINDOWS; ++position) {
            if (server->stack[position] == sibling) {
                sibling_position = position;
                break;
            }
        }
    }
    if (sibling_position >= 0) {
        target = sibling_position + (stack_mode == 0U ? 1 : 0);
    } else if (stack_mode == 0U) {
        for (int position = X_MAX_WINDOWS - 1; position >= 0; --position) {
            if (server->stack[position] >= 0) { target = position + 1; break; }
        }
    } else {
        target = 0;
    }
    if (target < 0) target = 0;
    if (target >= X_MAX_WINDOWS) target = X_MAX_WINDOWS - 1;
    for (int move = X_MAX_WINDOWS - 1; move > target; --move)
        server->stack[move] = server->stack[move - 1];
    server->stack[target] = index;

    restack_child(server, index, sibling, stack_mode);
}

int x_window_map(struct x_server *server, int index) {
    struct x_window *window = &server->windows[index];
    if (window->mapped) return 1;
    window->mapped = 1U;
    stack_insert(server, index, -1, 0U);
    /* A window that appears under a stationary pointer owns the pointer
     * from that moment on. */
    server->pointer_window =
        x_window_find_at(server, server->pointer_x, server->pointer_y);
    for (int child = window->first_child; child >= 0;
         child = server->windows[child].next_sibling) {
        x_window_map(server, child);
    }
    x_event_map(server, index);
    /* A newly viewable window is exposed to the clients that asked for
     * exposure events. */
    x_event_expose(server, index, 0, 0, window->width, window->height);
    {
        int16_t origin_x = 0;
        int16_t origin_y = 0;
        x_window_absolute_origin(server, index, &origin_x, &origin_y);
        x_server_repaint_rect(server, origin_x, origin_y, window->width,
                               window->height);
    }
    return 1;
}

void x_window_unmap(struct x_server *server, int index) {
    struct x_window *window = &server->windows[index];
    if (!window->mapped) return;
    for (int child = window->first_child; child >= 0;
         child = server->windows[child].next_sibling) {
        x_window_unmap(server, child);
    }
    window->mapped = 0U;
    x_event_unmap(server, index);
    {
        int16_t origin_x = 0;
        int16_t origin_y = 0;
        x_window_absolute_origin(server, index, &origin_x, &origin_y);
        x_server_repaint_rect(server, origin_x, origin_y, window->width,
                               window->height);
    }
    for (int position = 0; position < X_MAX_WINDOWS; ++position) {
        if (server->stack[position] != index) continue;
        for (int move = position; move + 1 < X_MAX_WINDOWS; ++move) {
            server->stack[move] = server->stack[move + 1];
        }
        server->stack[X_MAX_WINDOWS - 1] = -1;
        break;
    }
}

void x_window_configure(struct x_server *server, int index, int16_t x, int16_t y,
                        uint16_t width, uint16_t height, uint16_t border_width,
                        uint32_t sibling, uint16_t stack_mode) {
    struct x_window *window = &server->windows[index];
    int resized = (window->width != width || window->height != height);
    window->x = x;
    window->y = y;
    window->border_width = border_width;
    if (resized) {
        release_backing(server, window);
        window->width = width;
        window->height = height;
        if (width && height) {
            window->backing = allocate_backing(server, width, height);
            window->backing_bytes = (size_t)width * (size_t)height *
                                    sizeof(uint32_t);
        }
    }
    if (stack_mode != 0xffffU && (stack_mode == 0U || stack_mode == 1U)) {
        int sibling_index = x_window_index(server, sibling);
        stack_insert(server, index, sibling_index, stack_mode);
    }
    x_event_configure(server, index);
}

void x_window_restack(struct x_server *server, int index, int sibling,
                      uint16_t stack_mode) {
    stack_insert(server, index, sibling, stack_mode);
}

void x_window_reparent(struct x_server *server, int index, int new_parent,
                       int16_t x, int16_t y) {
    int old_parent = server->windows[index].parent;
    unlink_from_parent(server, index);
    struct x_window *window = &server->windows[index];
    window->parent = new_parent;
    window->x = x;
    window->y = y;
    if (new_parent >= 0) {
        struct x_window *parent = &server->windows[new_parent];
        if (parent->first_child < 0) {
            parent->first_child = index;
        } else {
            int last = parent->first_child;
            while (server->windows[last].next_sibling >= 0) {
                last = server->windows[last].next_sibling;
            }
            server->windows[last].next_sibling = index;
            window->prev_sibling = last;
        }
    }
    x_event_reparent(server, index, old_parent);
    x_event_configure(server, index);
}

void x_window_absolute_origin(struct x_server *server, int index,
                              int16_t *out_x, int16_t *out_y) {
    int32_t total_x = 0;
    int32_t total_y = 0;
    int current = index;
    while (current >= 0 && current < X_MAX_WINDOWS) {
        total_x += server->windows[current].x;
        total_y += server->windows[current].y;
        current = server->windows[current].parent;
    }
    if (out_x) *out_x = (int16_t)total_x;
    if (out_y) *out_y = (int16_t)total_y;
}

/* Does the window cover the point? */
static int window_covers(struct x_server *server, int index, int16_t x,
                         int16_t y) {
    struct x_window *window = &server->windows[index];
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    x_window_absolute_origin(server, index, &origin_x, &origin_y);
    if (x < origin_x || y < origin_y) return 0;
    if (x >= origin_x + (int16_t)window->width) return 0;
    if (y >= origin_y + (int16_t)window->height) return 0;
    return 1;
}

/* The topmost mapped child of a window that covers the point.
 *
 * The child list runs from the bottom to the top of the stacking order, so
 * the search walks it backwards. */
static int child_covers(struct x_server *server, int parent, int16_t x,
                        int16_t y) {
    int last = server->windows[parent].first_child;
    if (last < 0) return -1;
    while (server->windows[last].next_sibling >= 0) {
        last = server->windows[last].next_sibling;
    }
    for (int child = last; child >= 0;
         child = server->windows[child].prev_sibling) {
        if (!server->windows[child].mapped) continue;
        if (window_covers(server, child, x, y)) return child;
    }
    return -1;
}

/* The deepest window that covers the point.
 *
 * The stacking order holds every window of the tree in one list, so it cannot
 * tell which child of a window is on top.  The tree does, and the deepest
 * window under the pointer is the one the pointer events belong to. */
int x_window_find_at(struct x_server *server, int16_t x, int16_t y) {
    int current = x_window_index(server, X_ROOT_WINDOW_ID);
    if (current < 0) return -1;
    for (;;) {
        int child = child_covers(server, current, x, y);
        if (child < 0) break;
        current = child;
    }
    return current;
}

int x_window_pointer_child(struct x_server *server, int16_t x, int16_t y) {
    int index = x_window_find_at(server, x, y);
    int root = x_window_index(server, X_ROOT_WINDOW_ID);
    if (index < 0 || index == root) return -1;
    return index;
}

/* ------------------------------------------------------------------ repaint */
void x_window_fill_backing(struct x_server *server, int index, uint32_t pixel) {
    if (index < 0 || index >= X_MAX_WINDOWS) return;
    struct x_window *window = &server->windows[index];
    if (!window->backing) return;
    size_t count = (size_t)window->width * (size_t)window->height;
    for (size_t position = 0U; position < count; ++position) {
        window->backing[position] = pixel;
    }
}

static void blit_window(struct x_server *server, int index, int16_t clip_x,
                        int16_t clip_y, uint16_t clip_width,
                        uint16_t clip_height) {
    struct x_window *window = &server->windows[index];
    if (!window->backing) return;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    x_window_absolute_origin(server, index, &origin_x, &origin_y);

    int32_t start_x = origin_x;
    int32_t start_y = origin_y;
    int32_t end_x = origin_x + window->width;
    int32_t end_y = origin_y + window->height;
    int32_t clip_start_x = clip_x;
    int32_t clip_start_y = clip_y;
    int32_t clip_end_x = clip_x + clip_width;
    int32_t clip_end_y = clip_y + clip_height;

    if (start_x < clip_start_x) start_x = clip_start_x;
    if (start_y < clip_start_y) start_y = clip_start_y;
    if (end_x > clip_end_x) end_x = clip_end_x;
    if (end_y > clip_end_y) end_y = clip_end_y;
    if (start_x >= end_x || start_y >= end_y) return;

    for (int32_t target_y = start_y; target_y < end_y; ++target_y) {
        if (target_y < 0 || target_y >= server->height) continue;
        uint32_t source_row = (uint32_t)(target_y - origin_y);
        for (int32_t target_x = start_x; target_x < end_x; ++target_x) {
            if (target_x < 0 || target_x >= server->width) continue;
            uint32_t source_column = (uint32_t)(target_x - origin_x);
            server->screen[(size_t)target_y * server->width + target_x] =
                window->backing[(size_t)source_row * window->width +
                                source_column];
        }
    }
}

static void repaint_window_tree(struct x_server *server, int index,
                                int16_t x, int16_t y, uint16_t width,
                                uint16_t height, int depth) {
    if (index < 0 || index >= X_MAX_WINDOWS || depth >= X_MAX_WINDOWS) return;
    struct x_window *window = &server->windows[index];
    if (!window->mapped) return;

    blit_window(server, index, x, y, width, height);
    for (int child = window->first_child; child >= 0;
         child = server->windows[child].next_sibling) {
        repaint_window_tree(server, child, x, y, width, height, depth + 1);
    }
}

/* Repaint only the given screen rectangle.
 *
 * The screen lives in shared memory that the compositor maps as well, so the
 * server damages only what changed instead of rewriting the whole frame. */
void x_server_repaint_rect(struct x_server *server, int16_t x, int16_t y,
                           uint16_t width, uint16_t height) {
    if (!server->screen) return;
    if (width == 0U || height == 0U) return;

    int32_t start_x = x;
    int32_t start_y = y;
    int32_t end_x = x + width;
    int32_t end_y = y + height;
    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;
    if (end_x > server->width) end_x = server->width;
    if (end_y > server->height) end_y = server->height;
    if (start_x >= end_x || start_y >= end_y) return;

    int root = x_window_index(server, X_ROOT_WINDOW_ID);
    uint32_t background = root >= 0 ? server->windows[root].background_pixel : 0U;
    for (int32_t row = start_y; row < end_y; ++row) {
        uint32_t *line = server->screen + (size_t)row * server->width;
        for (int32_t column = start_x; column < end_x; ++column) {
            line[column] = background;
        }
    }
    if (root >= 0) {
        repaint_window_tree(server, root, (int16_t)start_x, (int16_t)start_y,
                            (uint16_t)(end_x - start_x),
                            (uint16_t)(end_y - start_y), 0);
    } else {
        for (int position = 0; position < X_MAX_WINDOWS; ++position) {
            int index = server->stack[position];
            if (index < 0 || !server->windows[index].mapped) continue;
            blit_window(server, index, (int16_t)start_x, (int16_t)start_y,
                        (uint16_t)(end_x - start_x),
                        (uint16_t)(end_y - start_y));
        }
    }
    x_server_publish_damage(server);
}

/* Record the screen area that a drawing operation changed.
 *
 * Drawing coordinates are relative to the drawable, so the rectangle is
 * translated into screen space before it is recorded. */
void x_server_damage(struct x_server *server, int window_index, int16_t x,
                     int16_t y, int16_t width, int16_t height) {
    if (width <= 0 || height <= 0) return;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    if (window_index >= 0) {
        x_window_absolute_origin(server, window_index, &origin_x, &origin_y);
    }
    int32_t start_x = origin_x + x;
    int32_t start_y = origin_y + y;
    int32_t end_x = start_x + width;
    int32_t end_y = start_y + height;
    if (server->dirty_width <= 0 || server->dirty_height <= 0) {
        server->dirty_x = start_x;
        server->dirty_y = start_y;
        server->dirty_width = end_x - start_x;
        server->dirty_height = end_y - start_y;
        return;
    }
    int32_t current_end_x = server->dirty_x + server->dirty_width;
    int32_t current_end_y = server->dirty_y + server->dirty_height;
    if (start_x < server->dirty_x) {
        server->dirty_width += server->dirty_x - start_x;
        server->dirty_x = start_x;
    }
    if (start_y < server->dirty_y) {
        server->dirty_height += server->dirty_y - start_y;
        server->dirty_y = start_y;
    }
    if (end_x > current_end_x) server->dirty_width = end_x - server->dirty_x;
    if (end_y > current_end_y) server->dirty_height = end_y - server->dirty_y;
}

void x_server_repaint(struct x_server *server) {
    if (!server->screen) return;
    if (server->dirty_width > 0 && server->dirty_height > 0) {
        x_server_repaint_rect(server, (int16_t)server->dirty_x,
                              (int16_t)server->dirty_y,
                              (uint16_t)server->dirty_width,
                              (uint16_t)server->dirty_height);
        server->dirty_width = 0;
        server->dirty_height = 0;
        return;
    }
    x_server_repaint_rect(server, 0, 0, (uint16_t)server->width,
                           (uint16_t)server->height);
}
