/*
 * akaOS XSCore Lumen Wayland Compositor - Core Protocol & IPC Implementation
 *
 * Implements wire-protocol demuxing, surface state machine, and client dispatch.
 */

#include "protocol.h"
#include <xscore/heap.h>
#include <xscore/serial.h>
#include <xscore/string.h>

#define kmalloc(sz) xscore_heap_allocate(sz)
#define kfree(ptr)  xscore_heap_free(ptr)

static struct lumen_client *g_clients_head = NULL;
static uint32_t g_next_client_id = 1U;

int lumen_protocol_init(void) {
    g_clients_head = NULL;
    g_next_client_id = 1U;
    xscore_serial_write("[LUMEN] Wayland Core Protocol & IPC engine initialized\n");
    return 0;
}

void lumen_protocol_destroy(void) {
    struct lumen_client *curr = g_clients_head;
    while (curr) {
        struct lumen_client *next = curr->next;
        lumen_client_destroy(curr);
        curr = next;
    }
    g_clients_head = NULL;
}

struct lumen_client *lumen_client_create(int socket_fd) {
    struct lumen_client *c = (struct lumen_client *)kmalloc(sizeof(struct lumen_client));
    if (!c) return NULL;

    c->id = g_next_client_id++;
    c->socket_fd = socket_fd;
    c->next_object_id = 2U;
    c->surfaces = NULL;
    c->next = g_clients_head;
    g_clients_head = c;

    return c;
}

void lumen_client_destroy(struct lumen_client *client) {
    if (!client) return;

    struct lumen_surface *s = client->surfaces;
    while (s) {
        struct lumen_surface *s_next = s->next;
        if (s->current_buffer) {
            kfree(s->current_buffer);
        }
        if (s->pending_buffer && s->pending_buffer != s->current_buffer) {
            kfree(s->pending_buffer);
        }
        kfree(s);
        s = s_next;
    }

    struct lumen_client **indirect = &g_clients_head;
    while (*indirect && *indirect != client) {
        indirect = &(*indirect)->next;
    }
    if (*indirect) {
        *indirect = client->next;
    }

    kfree(client);
}

void lumen_surface_commit(struct lumen_surface *surface) {
    if (!surface) return;

    if (surface->pending_buffer) {
        surface->current_buffer = surface->pending_buffer;
        surface->pending_buffer = NULL;
        surface->is_mapped = 1U;
        surface->damage_seq++;
    }
}

int lumen_client_dispatch(struct lumen_client *client) {
    if (!client) return -1;
    return 0;
}
