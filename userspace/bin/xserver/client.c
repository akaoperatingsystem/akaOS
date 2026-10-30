/* Client connection handling, the X11 setup handshake and request dispatch. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"
#include "setup.h"

#define X_MAX_REQUEST_UNITS 65535U

static void client_reset(struct x_client *client) {
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

/* -------------------------------------------------------------- byte order */
/* The byte order helpers live in server.h so every translation unit of the
 * server can convert values for the client. */

/* ------------------------------------------------------------------ output */
int x_client_write(struct x_client *client, const void *data, size_t length) {
    if (!client || !data) return 0;
    const uint8_t *bytes = data;
    size_t written = 0U;
    while (written < length) {
        ssize_t result = write(client->fd, bytes + written, length - written);
        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && (errno == EINTR)) continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd descriptor;
            descriptor.fd = client->fd;
            descriptor.events = POLLOUT;
            descriptor.revents = 0;
            if (poll(&descriptor, 1, 2000) > 0) continue;
        }
        return 0;
    }
    return 1;
}

void x_client_flush(struct x_client *client) {
    if (!client || client->output_length == 0U) return;
    size_t written = 0U;
    while (written < client->output_length) {
        ssize_t result = write(client->fd, client->output + written,
                                client->output_length - written);
        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        if (result == 0 || (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            /* The peer has no room for the moment.  A reply that is cut
             * short would leave the client waiting for the rest of it, and
             * the client would read the next answer at the wrong place. */
            struct pollfd descriptor;
            descriptor.fd = client->fd;
            descriptor.events = POLLOUT;
            descriptor.revents = 0;
            if (poll(&descriptor, 1, 2000) > 0) continue;
            break;
        }
        break;
    }
    if (written < client->output_length) {
        printf("xserver: client output stalled after %zu of %zu bytes (error %d)\n",
               written, client->output_length, errno);
        fflush(stdout);
        /* The rest of the answer stays in the buffer and is sent with the
         * next flush. */
        memmove(client->output, client->output + written,
                client->output_length - written);
        client->output_length -= written;
        return;
    }
    client->output_length = 0U;
}

void x_server_flush_clients(struct x_server *server) {
    for (int index = 0; index < X_MAX_CLIENTS; ++index) {
        if (server->clients[index].in_use) {
            x_client_flush(&server->clients[index]);
        }
    }
}

void x_client_queue(struct x_client *client, const void *data,
                    size_t length) {
    if (!client || length == 0U) return;
    if (client->output_length + length > client->output_capacity) {
        size_t capacity = client->output_capacity ? client->output_capacity : 8192U;
        while (capacity < client->output_length + length) capacity *= 2U;
        uint8_t *grown = realloc(client->output, capacity);
        if (!grown) return;
        client->output = grown;
        client->output_capacity = capacity;
    }
    memcpy(client->output + client->output_length, data, length);
    client->output_length += length;
}

/* Read exactly the requested number of bytes.
 *
 * A stream socket may deliver a request in several pieces and a descriptor
 * that is not in blocking mode can report EAGAIN, so the read waits for the
 * remaining bytes instead of giving up on a short transfer. */
static int read_exact(int fd, void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    size_t filled = 0U;
    while (filled < length) {
        ssize_t result = read(fd, bytes + filled, length - filled);
        if (result > 0) {
            filled += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd descriptor;
            descriptor.fd = fd;
            descriptor.events = POLLIN;
            descriptor.revents = 0;
            if (poll(&descriptor, 1, 1000) > 0) continue;
            return 0;
        }
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ replies */
/* An X11 reply is a 32 byte header: type, detail, sequence number, the
 * length of the additional data in four byte units and 24 bytes of
 * padding.  The upstream reply structures already include that header, so
 * only the bare header is assembled here. */
static void reply_header(struct x_client *client, uint8_t kind, uint8_t detail,
                         uint16_t length_units) {
    uint8_t header[32];
    memset(header, 0, sizeof(header));
    header[0] = kind;
    header[1] = detail;
    uint16_t sequence = client_u16(client, (uint16_t)client->sequence);
    uint16_t units = client_u16(client, length_units);
    memcpy(header + 2, &sequence, 2);
    memcpy(header + 6, &units, 2);
    x_client_queue(client, header, sizeof(header));
}

void x_reply_error(struct x_client *client, uint8_t error_code, uint8_t minor,
                   uint32_t resource) {
    (void)minor;
    uint8_t payload[32];
    memset(payload, 0, sizeof(payload));
    payload[0] = X_Error;
    payload[1] = error_code;
    uint16_t sequence = client_u16(client, (uint16_t)client->sequence);
    uint32_t resource_value = client_u32(client, resource);
    /* The server implements no extension, so every request is a core request.
     * A core request has no minor opcode, and the protocol puts the major
     * opcode into the minor opcode field of the error. */
    uint16_t minor_opcode = client_u16(client, client->request_major);
    memcpy(payload + 2, &sequence, 2);
    memcpy(payload + 4, &resource_value, 4);
    memcpy(payload + 8, &minor_opcode, 2);
    payload[10] = client->request_major;
    if (server_trace_enabled) {
        printf("xserver: c%d error %u seq %u\n", client->fd,
               (unsigned)error_code, (unsigned)client->sequence);
        fflush(stdout);
    }
    x_client_queue(client, payload, sizeof(payload));
}

void x_reply_simple(struct x_client *client, uint8_t detail) {
    reply_header(client, X_Reply, detail, 0U);
}

void x_reply_bytes(struct x_client *client, const void *payload, size_t length) {
    if (server_trace_enabled && client->traced_sequence != client->sequence) {
        /* The data of a reply follows its header in several pieces, and the
         * trace writes one line for the reply and not one for every piece. */
        client->traced_sequence = client->sequence;
        printf("xserver: t%llu c%d reply seq %u bytes %zu\n",
               (unsigned long long)server_now_millis(), client->fd,
               (unsigned)client->sequence, length);
        fflush(stdout);
    }
    x_client_queue(client, payload, length);
}

/* ------------------------------------------------------------ setup reply */
static void send_setup(struct x_server *server, struct x_client *client) {
    (void)server;
    static const char vendor[] = "The akaOS Project";
    uint8_t buffer[512];
    size_t length = x_setup_build(buffer, sizeof(buffer), server->width,
                                  server->height, client->resource_base,
                                  client->resource_mask, client->byte_order,
                                  vendor);
    if (length == 0U) return;
    x_client_queue(client, buffer, length);
}

static int send_setup_failure(struct x_client *client) {
    uint8_t payload[8];
    payload[0] = 0U;                   /* failed */
    payload[1] = 0U;
    payload[2] = 0U;
    payload[3] = 0U;
    uint16_t length = client_u16(client, 1U);
    memcpy(payload + 4, &length, 2);
    payload[6] = 3U;                   /* Failed */
    payload[7] = 0U;
    x_client_queue(client, payload, sizeof(payload));
    return 0;
}

/* --------------------------------------------------------------- lifecycle */
void x_client_accept(struct x_server *server, int fd) {
    int slot = -1;
    for (int index = 0; index < X_MAX_CLIENTS; ++index) {
        if (!server->clients[index].in_use) { slot = index; break; }
    }
    if (slot < 0) {
        close(fd);
        return;
    }
    struct x_client *client = &server->clients[slot];
    client_reset(client);
    client->fd = fd;
    client->in_use = 1;
    /* Every connection is given its own base of identifiers, so that a client
     * that connects later never walks into the identifiers of a client that
     * went away. */
    server->next_resource_base += 0x01000000U;
    if (server->next_resource_base > 0x70000000U) {
        server->next_resource_base = 0x20000000U;
    }
    client->resource_base = server->next_resource_base;
    client->resource_mask = 0x00ffffffU;

    /* Read the setup request header. */
    uint8_t header[12];
    if (!read_exact(fd, header, sizeof(header))) {
        close(fd);
        client->in_use = 0;
        return;
    }
    client->byte_order = header[0];
    uint16_t name_length = (uint16_t)(header[6] | ((uint16_t)header[7] << 8));
    uint16_t data_length = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));
    size_t padding = ((size_t)name_length + 3U) & ~(size_t)3U;
    padding += ((size_t)data_length + 3U) & ~(size_t)3U;
    uint8_t *auth = malloc(padding ? padding : 1U);
    uint8_t *auth_name = NULL;
    if (auth) {
        if (read_exact(fd, auth, padding)) auth_name = auth;
    }

    /* Only the standard authorization protocol is understood.  A client that
     * asks for anything else is refused instead of being silently admitted. */
    int authorized = 1;
    if (name_length > 0U) {
        static const char magic[] = "MIT-MAGIC-COOKIE-1";
        authorized = (name_length == (uint16_t)(sizeof(magic) - 1U) &&
                      memcmp(auth_name, magic, sizeof(magic) - 1U) == 0);
    }
    free(auth);
    if (!authorized || (client->byte_order != X_LSB_FIRST &&
                        client->byte_order != X_MSB_FIRST)) {
        (void)send_setup_failure(client);
        x_client_flush(client);
        x_client_disconnect(server, client);
        return;
    }
    send_setup(server, client);
    printf("xserver: client %d connected and setup sent\n",
           client_index_of(server, client));
    fflush(stdout);
}

void x_client_disconnect(struct x_server *server, struct x_client *client) {
    if (!client || !client->in_use) return;
    int index = client_index_of(server, client);
    if (server_trace_enabled) {
        printf("xserver: disconnecting client %d with descriptor %d\n",
               index, client->fd);
        fflush(stdout);
    }
    /* The event selections the client holds have to go with it, otherwise a
     * client that connects later inherits the selections of the one before. */
    x_window_drop_client_selections(server, index);
    /* The resources of the client go with it as well.  A window that stays
     * behind holds on to its identifier, and a client that connects later and
     * is given the same base of identifiers cannot create a window of its
     * own, because the server still knows the identifier of the window of
     * the client that went away. */
    for (;;) {
        int found = -1;
        for (int window = 0; window < X_MAX_WINDOWS; ++window) {
            if (server->windows[window].id == 0U) continue;
            if (server->windows[window].client != index) continue;
            found = window;
            break;
        }
        if (found < 0) break;
        x_window_destroy(server, found);
    }
    for (int pixmap = 0; pixmap < X_MAX_PIXMAPS; ++pixmap) {
        if (server->pixmaps[pixmap].id == 0U) continue;
        if (server->pixmaps[pixmap].client != index) continue;
        if (server->backing_used >= server->pixmaps[pixmap].bytes) {
            server->backing_used -= server->pixmaps[pixmap].bytes;
        }
        free(server->pixmaps[pixmap].pixels);
        memset(&server->pixmaps[pixmap], 0, sizeof(server->pixmaps[pixmap]));
    }
    for (int gc = 0; gc < X_MAX_GCS; ++gc) {
        if (server->gcs[gc].id == 0U) continue;
        if (server->gcs[gc].client != index) continue;
        memset(&server->gcs[gc], 0, sizeof(server->gcs[gc]));
    }
    for (int cursor = 0; cursor < X_MAX_CURSORS; ++cursor) {
        if (server->cursors[cursor].id == 0U) continue;
        if (server->cursors[cursor].client != index) continue;
        memset(&server->cursors[cursor], 0, sizeof(server->cursors[cursor]));
    }
    for (int font = 0; font < X_MAX_FONTS; ++font) {
        if (server->fonts[font].id == 0U) continue;
        if (server->fonts[font].client != index) continue;
        memset(&server->fonts[font], 0, sizeof(server->fonts[font]));
    }
    for (int colormap = 0; colormap < X_MAX_COLORMAPS; ++colormap) {
        if (server->colormaps[colormap].id == 0U) continue;
        if (server->colormaps[colormap].client != index) continue;
        memset(&server->colormaps[colormap], 0, sizeof(server->colormaps[colormap]));
    }
    for (int selection = 0; selection < X_MAX_SELECTIONS; ++selection) {
        if (server->selections[selection].selection == 0U) continue;
        if (server->selections[selection].owner != 0U &&
            x_window_index(server, server->selections[selection].owner) < 0) {
            server->selections[selection].owner = 0U;
        }
    }
    if (client->fd >= 0) close(client->fd);
    free(client->output);
    client_reset(client);
}

/* ---------------------------------------------------------- request decode */
struct x_request {
    uint8_t type;
    uint8_t detail;
    uint16_t length_units;
    const uint8_t *body;
    size_t body_length;
};

static int read_request(struct x_client *client, struct x_request *request) {
    uint8_t header[4];
    if (!read_exact(client->fd, header, sizeof(header))) {
        printf("xserver: client on descriptor %d closed the connection at a "
               "request header\n", client->fd);
        fflush(stdout);
        return 0;
    }
    request->type = header[0];
    request->detail = header[1];
    request->length_units = (uint16_t)(header[2] | ((uint16_t)header[3] << 8));
    size_t total = (size_t)request->length_units * 4U;
    if (total < 4U) total = 4U;
    size_t body_length = total - 4U;
    request->body = NULL;
    request->body_length = body_length;
    if (body_length == 0U) return 1;

    uint8_t *body = malloc(body_length);
    if (!body) return 0;
    if (!read_exact(client->fd, body, body_length)) {
        printf("xserver: client on descriptor %d closed the connection inside "
               "the body of request %u\n", client->fd, (unsigned)request->type);
        fflush(stdout);
        free(body);
        return 0;
    }
    request->body = body;
    return 1;
}

/* Include the request dispatcher. */
#include "xcore.h"
#include "dispatch.inc"

/* ------------------------------------------------------------------ service */
/* Report whether more data waits on the connection of a client.
 *
 * The display server reads one request per poll round, so a client that sends
 * a burst of requests would be held back by the poll timeout on every one of
 * them.  The server asks the connection whether more data arrived before it
 * goes back to the poll. */
static int client_has_pending(struct x_client *client) {
    struct pollfd descriptor;
    descriptor.fd = client->fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
}

void x_client_service(struct x_server *server, struct x_client *client) {
    if (!client || !client->in_use) return;
    /* Every request that has already arrived is served, so that a client
     * that sends a burst of requests is not held back by the poll timeout. */
    for (;;) {
        struct x_request request;
        int ok = read_request(client, &request);
        if (!ok) {
            x_client_disconnect(server, client);
            return;
        }
        x_dispatch(server, client, &request);
        free((void *)request.body);
        if (!client_has_pending(client)) return;
    }
}
