#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <unistd.h>

#define X11_SOCKET_PATH "/run/X11-unix/X0"
#define WAYLAND_SOCKET_PATH "/run/user/0/wayland-0"
#define DISPLAY_BRIDGE_MARKER "/akaos-display-protocols"
#define X11_MAX_PACKET 4096U
#define WAYLAND_MAX_PACKET 4096U

static uint16_t read16(const unsigned char *bytes, int big) {
    if (big) return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return (uint16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

static void write16(unsigned char *bytes, uint16_t value, int big) {
    if (big) {
        bytes[0] = (unsigned char)(value >> 8);
        bytes[1] = (unsigned char)value;
    } else {
        bytes[0] = (unsigned char)value;
        bytes[1] = (unsigned char)(value >> 8);
    }
}

static uint32_t read32(const unsigned char *bytes, int big) {
    if (big) return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                       ((uint32_t)bytes[2] << 8) | bytes[3];
    return ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[1] << 8) | bytes[0];
}

static void write32(unsigned char *bytes, uint32_t value, int big) {
    if (big) {
        bytes[0] = (unsigned char)(value >> 24);
        bytes[1] = (unsigned char)(value >> 16);
        bytes[2] = (unsigned char)(value >> 8);
        bytes[3] = (unsigned char)value;
    } else {
        bytes[0] = (unsigned char)value;
        bytes[1] = (unsigned char)(value >> 8);
        bytes[2] = (unsigned char)(value >> 16);
        bytes[3] = (unsigned char)(value >> 24);
    }
}

static size_t align4(size_t value) { return (value + 3U) & ~3U; }

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *bytes = (unsigned char *)buffer;
    size_t received = 0;
    while (received < length) {
        ssize_t result = recv(fd, bytes + received, length - received, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        received += (size_t)result;
    }
    return 1;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(fd, bytes + sent, length - sent, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        sent += (size_t)result;
    }
    return 1;
}

static int open_unix_listener(const char *path) {
    if (!path || strlen(path) >= UNIX_PATH_MAX) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    /* The kernel owns the endpoint namespace; no host filesystem unlink is
     * needed and keeping the copy byte-wise avoids alignment-sensitive ARM
     * literal loads. */
    volatile char *destination = address.sun_path;
    size_t index = 0;
    while (index + 1U < sizeof(address.sun_path) && path[index] != '\0') {
        destination[index] = path[index];
        ++index;
    }
    destination[index] = '\0';
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int publish_marker(void) {
    int descriptor = shm_open(DISPLAY_BRIDGE_MARKER, O_CREAT | O_RDWR, 0666);
    if (descriptor < 0) return 0;
    int result = ftruncate(descriptor, 1) == 0;
    close(descriptor);
    return result;
}

static void remove_marker(void) {
    (void)shm_unlink(DISPLAY_BRIDGE_MARKER);
}

static void x11_reply_header(unsigned char *reply, uint16_t sequence,
                             int big) {
    memset(reply, 0, 32U);
    reply[0] = 1;
    write16(reply + 2, sequence, big);
    write32(reply + 4, 0, big);
}

static int x11_send_setup(int fd, int big) {
    unsigned char response[256];
    memset(response, 0, sizeof(response));
    response[0] = 1; /* Success */
    write16(response + 2, 11, big);
    write16(response + 4, 0, big);
    /* The fixed header is 40 bytes; the payload below is 124 bytes. */
    write16(response + 6, 31, big);
    write32(response + 8, 1, big);             /* release */
    write32(response + 12, 0x01000000U, big); /* resource base */
    write32(response + 16, 0x00ffffffU, big); /* resource mask */
    write32(response + 20, 256, big);         /* motion buffer */
    const char vendor[] = "akaOS XSCore";
    write16(response + 24, (uint16_t)(sizeof(vendor) - 1U), big);
    write16(response + 26, 65535, big);
    response[28] = 1; /* one screen */
    response[29] = 1; /* one pixmap format */
    response[30] = (unsigned char)(big ? 1 : 0);
    response[31] = (unsigned char)(big ? 1 : 0);
    response[32] = 32;
    response[33] = 32;
    response[34] = 8;
    response[35] = 255;

    size_t offset = 40;
    memcpy(response + offset, vendor, sizeof(vendor) - 1U);
    offset += align4(sizeof(vendor) - 1U);
    response[offset + 0] = 24; /* depth */
    response[offset + 1] = 32; /* bits per pixel */
    response[offset + 2] = 32; /* scanline pad */
    offset += 8;

    /* Screen descriptor. */
    write32(response + offset + 0, 1, big);       /* root */
    write32(response + offset + 4, 1, big);       /* default colormap */
    write32(response + offset + 8, 0x00ffffffU, big);
    write32(response + offset + 12, 0, big);
    write32(response + offset + 16, 0, big);
    write16(response + offset + 20, 1024, big);
    write16(response + offset + 22, 768, big);
    write16(response + offset + 24, 270, big);
    write16(response + offset + 26, 203, big);
    write16(response + offset + 28, 1, big);
    write16(response + offset + 30, 1, big);
    write32(response + offset + 32, 0x20, big);
    response[offset + 36] = 0; /* backing stores */
    response[offset + 37] = 0; /* save unders */
    response[offset + 38] = 24;
    response[offset + 39] = 1; /* one allowed depth */
    offset += 40;
    response[offset + 0] = 24;
    response[offset + 2] = 1; /* one visual */
    offset += 8;
    write32(response + offset + 0, 0x20, big);
    response[offset + 4] = 4; /* TrueColor */
    response[offset + 5] = 8;
    write16(response + offset + 6, 256, big);
    write32(response + offset + 8, 0x00ff0000U, big);
    write32(response + offset + 12, 0x0000ff00U, big);
    write32(response + offset + 16, 0x000000ffU, big);
    offset += 24;
    return write_full(fd, response, offset);
}

static int x11_handle_client(int fd) {
    unsigned char setup[12];
    if (!read_full(fd, setup, sizeof(setup))) return 0;
    if (setup[0] != 'l' && setup[0] != 'B') return 0;
    int big = setup[0] == 'B';
    uint16_t auth_name_length = read16(setup + 6, big);
    uint16_t auth_data_length = read16(setup + 8, big);
    size_t name_bytes = align4(auth_name_length);
    size_t data_bytes = align4(auth_data_length);
    if (name_bytes > 256U || data_bytes > 256U) return 0;
    unsigned char auth[512];
    if (!read_full(fd, auth, name_bytes + data_bytes)) return 0;
    if (!x11_send_setup(fd, big)) return 0;

    uint16_t sequence = 0;
    uint32_t window_id = 0;
    int16_t window_x = 0;
    int16_t window_y = 0;
    uint16_t window_width = 1024;
    uint16_t window_height = 768;
    for (unsigned request_count = 0; request_count < 64U; ++request_count) {
        unsigned char header[4];
        if (!read_full(fd, header, sizeof(header))) return 1;
        uint16_t words = read16(header + 2, big);
        if (words == 0 || (size_t)words * 4U > X11_MAX_PACKET) return 0;
        size_t body_size = (size_t)words * 4U - 4U;
        unsigned char body[X11_MAX_PACKET];
        if (body_size && !read_full(fd, body, body_size)) return 0;
        ++sequence;
        if (header[0] == 43) { /* GetInputFocus */
            unsigned char reply[32];
            x11_reply_header(reply, sequence, big);
            reply[1] = 0;
            write32(reply + 8, 1, big);
            if (!write_full(fd, reply, sizeof(reply))) return 0;
        } else if (header[0] == 14) { /* GetGeometry */
            unsigned char reply[32];
            x11_reply_header(reply, sequence, big);
            reply[1] = 24;
            write32(reply + 8, 1, big);
            write16(reply + 12, (uint16_t)window_x, big);
            write16(reply + 14, (uint16_t)window_y, big);
            write16(reply + 16, window_width, big);
            write16(reply + 18, window_height, big);
            write16(reply + 20, 0, big);
            if (!write_full(fd, reply, sizeof(reply))) return 0;
        } else if (header[0] == 1) { /* CreateWindow */
            /* depth/pad, window, parent, x, y, width, height, border,
             * class, visual, value-mask, values... */
            if (body_size >= 28U) {
                window_id = read32(body + 4, big);
                window_x = (int16_t)read16(body + 12, big);
                window_y = (int16_t)read16(body + 14, big);
                window_width = read16(body + 16, big);
                window_height = read16(body + 18, big);
            }
        } else if (header[0] == 8) { /* MapWindow */
            /* Mapping is state-only in this bounded bridge. */
        } else if (header[0] == 12) { /* ConfigureWindow */
            if (body_size >= 8U) {
                uint32_t target = read32(body, big);
                uint16_t mask = read16(body + 4, big);
                if (target == window_id) {
                    size_t value_offset = 8U;
                    for (unsigned bit = 0; bit < 4U; ++bit) {
                        if (!(mask & (uint16_t)(1U << bit))) continue;
                        if (value_offset + 4U > body_size) break;
                        uint32_t value = read32(body + value_offset, big);
                        if (bit == 0) window_x = (int16_t)value;
                        else if (bit == 1) window_y = (int16_t)value;
                        else if (bit == 2) window_width = (uint16_t)value;
                        else if (bit == 3) window_height = (uint16_t)value;
                        value_offset += 4U;
                    }
                }
            }
        } else if (header[0] == 15) { /* QueryTree */
            unsigned char reply[32];
            x11_reply_header(reply, sequence, big);
            write32(reply + 8, 1, big);
            write32(reply + 12, 1, big);
            /* No child list is returned in this bounded bridge; the fixed
             * reply therefore advertises zero children. */
            write16(reply + 16, 0, big);
            if (!write_full(fd, reply, sizeof(reply))) return 0;
        } else if (header[0] == 16) { /* InternAtom */
            unsigned char reply[32];
            x11_reply_header(reply, sequence, big);
            write32(reply + 8, 1, big);
            if (!write_full(fd, reply, sizeof(reply))) return 0;
        } else if (header[0] == 98) { /* QueryExtension */
            unsigned char reply[32];
            x11_reply_header(reply, sequence, big);
            reply[1] = 1;
            reply[8] = 1;  /* extension major opcode */
            reply[9] = 0;  /* first event */
            reply[10] = 0; /* first error */
            if (!write_full(fd, reply, sizeof(reply))) return 0;
        }
        /* CreateWindow, MapWindow, and the common setup requests are
         * deliberately state-light: accepting and consuming them gives
         * toolkit clients a usable root without fabricating server state. */
    }
    return 1;
}

static size_t wayland_string(unsigned char *buffer, size_t offset,
                             const char *value) {
    /* Wayland wire strings include the terminating NUL in their length. */
    size_t length = strlen(value) + 1U;
    write32(buffer + offset, (uint32_t)length, 0);
    offset += 4;
    memcpy(buffer + offset, value, length - 1U);
    buffer[offset + length - 1U] = 0;
    offset += length;
    while ((offset & 3U) != 0) buffer[offset++] = 0;
    return offset;
}

static int wayland_send_global(int fd, uint32_t registry, uint32_t name,
                               const char *interface_name, uint32_t version) {
    unsigned char message[256];
    memset(message, 0, sizeof(message));
    write32(message, registry, 0);
    size_t offset = 8;
    write32(message + offset, name, 0);
    offset += 4;
    offset = wayland_string(message, offset, interface_name);
    write32(message + offset, version, 0);
    offset += 4;
    write32(message + 4, (uint32_t)(offset << 16), 0);
    return write_full(fd, message, offset);
}

static int wayland_send_done(int fd, uint32_t callback, uint32_t serial) {
    unsigned char message[16];
    memset(message, 0, sizeof(message));
    write32(message, callback, 0);
    write32(message + 4, (uint32_t)(12U << 16), 0);
    write32(message + 8, serial, 0);
    return write_full(fd, message, 12);
}

static int wayland_send_ping(int fd, uint32_t object, uint32_t serial) {
    unsigned char message[12];
    memset(message, 0, sizeof(message));
    write32(message, object, 0);
    write32(message + 4, 12U << 16, 0);
    write32(message + 8, serial, 0);
    return write_full(fd, message, sizeof(message));
}

static int wayland_send_toplevel_configure(int fd, uint32_t object,
                                           uint32_t width, uint32_t height) {
    unsigned char message[24];
    memset(message, 0, sizeof(message));
    write32(message, object, 0);
    /* xdg_toplevel.configure(width, height, states[]), empty states array. */
    write32(message + 4, 20U << 16, 0);
    write32(message + 8, width, 0);
    write32(message + 12, height, 0);
    write32(message + 16, 0, 0);
    return write_full(fd, message, 20U);
}

static int wayland_send_surface_configure(int fd, uint32_t object,
                                          uint32_t serial) {
    unsigned char message[12];
    memset(message, 0, sizeof(message));
    write32(message, object, 0);
    write32(message + 4, 12U << 16, 0);
    write32(message + 8, serial, 0);
    return write_full(fd, message, sizeof(message));
}

static int wayland_read_string(const unsigned char *body, size_t body_size,
                               size_t *offset, char *destination,
                               size_t capacity) {
    if (!body || !offset || !destination || capacity == 0 ||
        *offset + 4U > body_size) return 0;
    uint32_t length = read32(body + *offset, 0);
    *offset += 4U;
    if (length == 0 || length > 256U || *offset > body_size - length)
        return 0;
    size_t copy = length < capacity - 1U ? length : capacity - 1U;
    memcpy(destination, body + *offset, copy);
    destination[copy] = '\0';
    *offset += align4(length);
    return *offset <= body_size;
}

static int wayland_handle_client(int fd) {
    uint32_t serial = 1;
    uint32_t compositor = 0;
    uint32_t core_surface = 0;
    uint32_t xdg_wm_base = 0;
    uint32_t xdg_surface = 0;
    uint32_t xdg_toplevel = 0;
    for (unsigned request_count = 0; request_count < 64U; ++request_count) {
        unsigned char header[8];
        if (!read_full(fd, header, sizeof(header))) return 1;
        uint32_t size_opcode = read32(header + 4, 0);
        uint32_t size = size_opcode >> 16;
        uint32_t opcode = size_opcode & 0xffffU;
        if (size < 8U || size > WAYLAND_MAX_PACKET) return 0;
        size_t body_size = size - 8U;
        unsigned char body[WAYLAND_MAX_PACKET];
        if (body_size && !read_full(fd, body, body_size)) return 0;
        uint32_t object = read32(header, 0);
        if (object == 1U && opcode == 1U && body_size >= 4U) {
            uint32_t registry = read32(body, 0);
            if (!wayland_send_global(fd, registry, 1, "wl_compositor", 4) ||
                !wayland_send_global(fd, registry, 2, "wl_shm", 1) ||
                !wayland_send_global(fd, registry, 3, "wl_seat", 7) ||
                !wayland_send_global(fd, registry, 4, "xdg_wm_base", 6)) return 0;
        } else if (object == 1U && opcode == 0U && body_size >= 4U) {
            /* wl_display.sync(callback). */
            if (!wayland_send_done(fd, read32(body, 0), serial++)) return 0;
        } else if (object == 1U && opcode == 1U) {
            /* get_registry was handled above; keep malformed repeats benign. */
        } else if (object != 1U && opcode == 0U && body_size >= 4U &&
                   object == compositor) {
            /* wl_compositor.create_surface(new_id). */
            core_surface = read32(body, 0);
        } else if (object != 1U && opcode == 0U && body_size >= 4U &&
                   object == xdg_wm_base) {
            /* xdg_wm_base.destroy(). */
            xdg_wm_base = 0;
        } else if (object != 1U && object == xdg_wm_base && opcode == 3U &&
                   body_size >= 4U) {
            /* xdg_wm_base.pong(serial). */
        } else if (object == xdg_wm_base && opcode == 2U && body_size >= 8U) {
            /* xdg_wm_base.get_xdg_surface(new_id, wl_surface). */
            xdg_surface = read32(body, 0);
        } else if (object == xdg_surface && opcode == 1U && body_size >= 4U) {
            /* xdg_surface.get_toplevel(new_id). */
            xdg_toplevel = read32(body, 0);
            if (!wayland_send_toplevel_configure(fd, xdg_toplevel, 1024, 768) ||
                !wayland_send_surface_configure(fd, xdg_surface, serial++))
                return 0;
        } else if (object == xdg_surface && opcode == 4U && body_size >= 4U) {
            /* xdg_surface.ack_configure(serial). */
        } else if (object == xdg_toplevel && opcode == 2U) {
            /* xdg_toplevel.set_title(title). */
        } else if (object == core_surface && opcode == 6U) {
            /* wl_surface.commit; Lumen owns the actual shared pixels. */
        } else if (object != 1U && opcode == 0U && body_size >= 20U) {
            /* registry.bind(name, interface, version, new_id).  Decode the
             * string rather than keying on packet offsets so padded names are
             * accepted exactly like real Wayland clients. */
            size_t offset = 0;
            uint32_t global_name = read32(body + offset, 0);
            offset += 4U;
            char interface_name[64];
            if (!wayland_read_string(body, body_size, &offset, interface_name,
                                     sizeof(interface_name)) ||
                offset + 8U > body_size) continue;
            (void)read32(body + offset, 0);
            offset += 4U;
            uint32_t new_id = read32(body + offset, 0);
            if (global_name == 1U && strcmp(interface_name, "wl_compositor") == 0)
                compositor = new_id;
            else if (global_name == 4U && strcmp(interface_name, "xdg_wm_base") == 0) {
                xdg_wm_base = new_id;
                if (!wayland_send_ping(fd, xdg_wm_base, serial++)) return 0;
            }
        }
    }
    return 1;
}

static void usage(void) {
    puts("Usage: display-protocols [--x11|--wayland|--both] [--once]\n"
         "Native akaOS X11 setup and Wayland wire-protocol bridge.");
}

int main(int argc, char **argv) {
    int want_x11 = 0;
    int want_wayland = 0;
    int once = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0 ||
            strcmp(argv[index], "-h") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[index], "--x11") == 0) want_x11 = 1;
        else if (strcmp(argv[index], "--wayland") == 0) want_wayland = 1;
        else if (strcmp(argv[index], "--both") == 0) {
            want_x11 = 1;
            want_wayland = 1;
        } else if (strcmp(argv[index], "--once") == 0) once = 1;
        else {
            fprintf(2, "display-protocols: unknown option '%s'\n", argv[index]);
            return 2;
        }
    }
    if (!want_x11 && !want_wayland) want_wayland = 1;

    int x11 = want_x11 ? open_unix_listener(X11_SOCKET_PATH) : -1;
    int wayland = want_wayland ? open_unix_listener(WAYLAND_SOCKET_PATH) : -1;
    if ((want_x11 && x11 < 0) || (want_wayland && wayland < 0)) {
        if (x11 >= 0) close(x11);
        if (wayland >= 0) close(wayland);
        fprintf(2, "display-protocols: unable to bind local display socket\n");
        return 1;
    }
    if (!publish_marker()) {
        if (x11 >= 0) close(x11);
        if (wayland >= 0) close(wayland);
        return 1;
    }
    puts("akaOS display protocol bridge ready");
    unsigned served_connections = 0;
    for (;;) {
        struct pollfd descriptors[2];
        nfds_t count = 0;
        if (x11 >= 0) {
            descriptors[count].fd = x11;
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            ++count;
        }
        if (wayland >= 0) {
            descriptors[count].fd = wayland;
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            ++count;
        }
        if (poll(descriptors, count, -1) < 0) continue;
        for (nfds_t index = 0; index < count; ++index) {
            if (!(descriptors[index].revents & POLLIN)) continue;
            int client = accept(descriptors[index].fd, NULL, NULL);
            if (client < 0) continue;
            if (descriptors[index].fd == x11) (void)x11_handle_client(client);
            else (void)wayland_handle_client(client);
            close(client);
            ++served_connections;
            if (once) {
                unsigned required_connections = (want_x11 && want_wayland) ? 2U : 1U;
                if (served_connections < required_connections) continue;
                if (x11 >= 0) close(x11);
                if (wayland >= 0) close(wayland);
                remove_marker();
                return 0;
            }
        }
    }
}
