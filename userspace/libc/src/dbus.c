#include <akaos/dbus.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__clang__)
#define AKAOS_DBUS_NOOPT __attribute__((noinline, optnone))
#else
#define AKAOS_DBUS_NOOPT
#endif

static int write_all_bytes(int descriptor, const void *buffer, size_t length) {
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(descriptor, bytes + written, length - written);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        written += (size_t)result;
    }
    return 1;
}

static void dbus_close_fd_array(int *fds, size_t *count) {
    if (!fds || !count) return;
    for (size_t index = 0; index < *count; ++index) {
        if (fds[index] >= 0) (void)close(fds[index]);
        fds[index] = -1;
    }
    *count = 0U;
}

static int dbus_send_bytes_with_fds(int descriptor, const void *buffer,
                                    size_t length, const int *fds,
                                    size_t fd_count) {
    if (fd_count == 0U) return write_all_bytes(descriptor, buffer, length);
    if (!buffer || length == 0U || !fds ||
        fd_count > AKAOS_DBUS_MAX_UNIX_FDS) {
        errno = EINVAL;
        return 0;
    }
    struct iovec vector = {(void *)buffer, length};
    unsigned char control[CMSG_SPACE(AKAOS_DBUS_MAX_UNIX_FDS * sizeof(int))];
    memset(control, 0, sizeof(control));
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1U;
    message.msg_control = control;
    message.msg_controllen = CMSG_SPACE(fd_count * sizeof(int));
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (!header) {
        errno = EINVAL;
        return 0;
    }
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(fd_count * sizeof(int));
    memcpy(CMSG_DATA(header), fds, fd_count * sizeof(int));
    ssize_t sent = sendmsg(descriptor, &message, 0);
    if (sent <= 0) return 0;
    return (size_t)sent == length ||
           write_all_bytes(descriptor, (const unsigned char *)buffer + sent,
                           length - (size_t)sent);
}

static int dbus_receive_chunk(int descriptor, void *buffer, size_t capacity,
                              size_t *received_out, int *fds,
                              size_t *fd_count, int allow_fds) {
    if (!buffer || capacity == 0U || !received_out || !fds || !fd_count)
        return 0;
    struct iovec vector = {buffer, capacity};
    unsigned char control[CMSG_SPACE(AKAOS_DBUS_MAX_UNIX_FDS * sizeof(int))];
    memset(control, 0, sizeof(control));
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1U;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t received;
    do {
        received = recvmsg(descriptor, &message, 0);
        if (received < 0 && (errno == EAGAIN || errno == EINTR))
            (void)sched_yield();
    } while (received < 0 && (errno == EAGAIN || errno == EINTR));
    if (received <= 0) return 0;
    int valid = (message.msg_flags & MSG_CTRUNC) == 0;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET ||
            header->cmsg_type != SCM_RIGHTS ||
            header->cmsg_len < CMSG_LEN(0U)) {
            valid = 0;
            continue;
        }
        size_t bytes = header->cmsg_len - CMSG_LEN(0U);
        if ((bytes % sizeof(int)) != 0U) {
            valid = 0;
            continue;
        }
        size_t count = bytes / sizeof(int);
        const int *items = (const int *)CMSG_DATA(header);
        for (size_t index = 0; index < count; ++index) {
            if (!allow_fds || *fd_count >= AKAOS_DBUS_MAX_UNIX_FDS) {
                (void)close(items[index]);
                valid = 0;
            } else {
                fds[(*fd_count)++] = items[index];
            }
        }
    }
    if (!valid) {
        dbus_close_fd_array(fds, fd_count);
        errno = EPROTO;
        return 0;
    }
    *received_out = (size_t)received;
    return 1;
}

static int read_exact_bytes(int descriptor, void *buffer, size_t length) {
    unsigned char *bytes = (unsigned char *)buffer;
    size_t received = 0;
    while (received < length) {
        ssize_t result = read(descriptor, bytes + received, length - received);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        received += (size_t)result;
    }
    return 1;
}

static int read_line_bytes(int descriptor, char *buffer, size_t capacity) {
    if (!buffer || capacity < 2) return 0;
    size_t length = 0;
    while (length + 1 < capacity) {
        char character;
        ssize_t result = read(descriptor, &character, 1);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        buffer[length++] = character;
        if (character == '\n') {
            buffer[length] = '\0';
            return 1;
        }
    }
    buffer[capacity - 1] = '\0';
    return 0;
}

static int dbus_connect_unix(const char *path) {
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    size_t path_length = strlen(path);
    if (path_length >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(address.sun_path, path, path_length + 1U);
    int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return -1;
    if (connect(descriptor, (const struct sockaddr *)&address,
                sizeof(address)) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static int dbus_connect_tcp_compat(int system_bus) {
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) return -1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(system_bus ? AKAOS_DBUS_SYSTEM_PORT :
                               AKAOS_DBUS_SESSION_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(descriptor, (const struct sockaddr *)&address,
                sizeof(address)) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static int dbus_connect_transport(int system_bus) {
    int descriptor = dbus_connect_unix(system_bus ?
        AKAOS_DBUS_SYSTEM_ENDPOINT : AKAOS_DBUS_SESSION_ENDPOINT);
    if (descriptor >= 0) return descriptor;
    return dbus_connect_tcp_compat(system_bus);
}

/* D-Bus EXTERNAL encodes the decimal uid as hexadecimal ASCII bytes. */
static size_t dbus_external_auth_line(char *line, size_t capacity) {
    static const char hex[] = "0123456789abcdef";
    if (!line || capacity < 20U) return 0;
    char uid_text[16];
    int uid_length = snprintf(uid_text, sizeof(uid_text), "%u",
                              (unsigned)geteuid());
    if (uid_length <= 0 || (size_t)uid_length >= sizeof(uid_text)) return 0;
    static const char prefix[] = "AUTH EXTERNAL ";
    size_t used = sizeof(prefix) - 1U;
    if (used + (size_t)uid_length * 2U + 3U > capacity) return 0;
    memcpy(line, prefix, used);
    for (int index = 0; index < uid_length; ++index) {
        unsigned char byte = (unsigned char)uid_text[index];
        line[used++] = hex[byte >> 4];
        line[used++] = hex[byte & 15U];
    }
    line[used++] = '\r';
    line[used++] = '\n';
    line[used] = '\0';
    return used;
}

static uint32_t dbus_read_le32(const unsigned char *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int dbus_byte_order_valid(int byte_order) {
    return byte_order == AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN ||
           byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
}

static uint32_t dbus_read_order32(const unsigned char *value,
                                  int byte_order) {
    if (byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN)
        return ((uint32_t)value[0] << 24U) |
               ((uint32_t)value[1] << 16U) |
               ((uint32_t)value[2] << 8U) | (uint32_t)value[3];
    return dbus_read_le32(value);
}

static void dbus_write_le32(unsigned char *destination, uint32_t value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void dbus_write_order32(unsigned char *destination, uint32_t value,
                               int byte_order) {
    if (byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN) {
        destination[0] = (unsigned char)(value >> 24U);
        destination[1] = (unsigned char)(value >> 16U);
        destination[2] = (unsigned char)(value >> 8U);
        destination[3] = (unsigned char)value;
    } else {
        dbus_write_le32(destination, value);
    }
}

static size_t dbus_align4(size_t value) { return (value + 3U) & ~3U; }
static size_t dbus_align8(size_t value) { return (value + 7U) & ~7U; }

static size_t dbus_append_uint32_field(unsigned char *header, size_t offset,
                                       unsigned char field_code,
                                       uint32_t value) {
    offset = dbus_align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1;
    header[offset++] = 'u';
    header[offset++] = 0;
    offset = dbus_align4(offset);
    dbus_write_le32(header + offset, value);
    return offset + 4U;
}

static size_t dbus_append_string_field(unsigned char *header, size_t offset,
                                       unsigned char field_code,
                                       char variant_type, const char *value) {
    size_t length = strlen(value);
    offset = dbus_align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1;
    header[offset++] = (unsigned char)variant_type;
    header[offset++] = 0;
    offset = dbus_align4(offset);
    dbus_write_le32(header + offset, (uint32_t)length);
    offset += 4;
    memcpy(header + offset, value, length);
    offset += length;
    header[offset++] = 0;
    return offset;
}

static size_t dbus_append_string_field_order(
    unsigned char *header, size_t offset, unsigned char field_code,
    char variant_type, const char *value, int byte_order) {
    size_t length = strlen(value);
    offset = dbus_align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1U;
    header[offset++] = (unsigned char)variant_type;
    header[offset++] = 0U;
    offset = dbus_align4(offset);
    dbus_write_order32(header + offset, (uint32_t)length, byte_order);
    offset += 4U;
    memcpy(header + offset, value, length);
    offset += length;
    header[offset++] = 0U;
    return offset;
}

static size_t dbus_append_signature_field(unsigned char *header, size_t offset,
                                          const char *signature) {
    size_t length = strlen(signature);
    if (length > 255U) return offset;
    offset = dbus_align8(offset);
    header[offset++] = 8;
    header[offset++] = 1;
    header[offset++] = 'g';
    header[offset++] = 0;
    header[offset++] = (unsigned char)length;
    memcpy(header + offset, signature, length);
    offset += length;
    header[offset++] = 0;
    return offset;
}

int akaos_dbus_connect(int system_bus) {
    int descriptor = dbus_connect_transport(system_bus);
    if (descriptor < 0) return -1;
    if (!write_all_bytes(descriptor, AKAOS_DBUS_NATIVE_MARKER,
                         sizeof(AKAOS_DBUS_NATIVE_MARKER) - 1U)) {
        close(descriptor);
        return -1;
    }
    char greeting[sizeof(AKAOS_DBUS_GREETING)];
    if (!read_exact_bytes(descriptor, greeting, sizeof(greeting) - 1U)) {
        close(descriptor);
        return -1;
    }
    greeting[sizeof(greeting) - 1U] = '\0';
    if (strncmp(greeting, AKAOS_DBUS_GREETING,
                sizeof(AKAOS_DBUS_GREETING) - 1U) != 0) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    return descriptor;
}

int akaos_dbus_ping(int descriptor) {
    static const char request[] = "PING\n";
    static const char response[] = "PONG\n";
    if (descriptor < 0 || !write_all_bytes(descriptor, request,
                                            sizeof(request) - 1U))
        return -1;
    char buffer[sizeof(response)];
    if (!read_exact_bytes(descriptor, buffer, sizeof(response) - 1U) ||
        memcmp(buffer, response, sizeof(response) - 1U) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

enum {
    AKAOS_DBUS_ASYNC_PENDING = 1U,
    AKAOS_DBUS_ASYNC_DONE = 2U,
    AKAOS_DBUS_ASYNC_ERROR = 3U,
};

int akaos_dbus_async_ping_init(struct akaos_dbus_async_ping *ping,
                               int descriptor) {
    static const unsigned char request[] = "PING\n";
    if (!ping || descriptor < 0) return -1;
    memset(ping, 0, sizeof(*ping));
    ping->descriptor = descriptor;
    ping->state = AKAOS_DBUS_ASYNC_PENDING;
    /* The five-byte request is intentionally written while the connection is
     * still in its normal blocking mode.  It is smaller than the kernel's
     * socket buffer, so initialization cannot consume an event-loop turn and
     * leaves the descriptor ready for a read-only watch. */
    if (!write_all_bytes(descriptor, request, sizeof(request) - 1U)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    return 0;
}

int akaos_dbus_async_ping_pump(struct akaos_dbus_async_ping *ping,
                               short revents) {
    if (!ping) return -1;
    if (ping->state == AKAOS_DBUS_ASYNC_DONE) return 1;
    if (ping->state != AKAOS_DBUS_ASYNC_PENDING) return -1;
    if (revents & (POLLERR | POLLNVAL)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }

    /* A GIO/GLib callback can be entered after a readiness edge has already
     * been consumed by another source.  Re-check with a zero-time poll and
     * use a short read so EAGAIN remains a normal pending result. */
    struct pollfd descriptor = {ping->descriptor, POLLIN, 0};
    int ready = poll(&descriptor, 1, 0);
    if (ready < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if (ready == 0) return 0;
    if (descriptor.revents & (POLLERR | POLLNVAL)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if ((descriptor.revents & POLLIN) == 0) {
        /* HUP without readable bytes is EOF; HUP with data is handled by the
         * read below so a final PONG is not discarded. */
        if (descriptor.revents & POLLHUP) {
            ping->state = AKAOS_DBUS_ASYNC_ERROR;
            return -1;
        }
        return 0;
    }
    ssize_t received = read(ping->descriptor,
                            ping->response + ping->received,
                            AKAOS_DBUS_ASYNC_PING_RESPONSE_SIZE -
                                ping->received);
    if (received < 0) {
        if (errno == EAGAIN || errno == EINTR) return 0;
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if (received == 0) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    ping->received += (size_t)received;
    if (ping->received < AKAOS_DBUS_ASYNC_PING_RESPONSE_SIZE) return 0;
    if (memcmp(ping->response, "PONG\n",
               AKAOS_DBUS_ASYNC_PING_RESPONSE_SIZE) != 0) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        errno = EIO;
        return -1;
    }
    ping->state = AKAOS_DBUS_ASYNC_DONE;
    return 1;
}

int akaos_dbus_async_ping_done(const struct akaos_dbus_async_ping *ping) {
    return ping && ping->state == AKAOS_DBUS_ASYNC_DONE;
}

int akaos_dbus_binary_ping(void) {
    /* Keep this conformance probe strict: it must exercise the standard
     * local UNIX transport rather than silently falling back to the legacy
     * akaOS TCP endpoint. */
    int descriptor = dbus_connect_unix(AKAOS_DBUS_SESSION_ENDPOINT);
    if (descriptor < 0) return -1;
    char auth_line[64];
    size_t auth_length = dbus_external_auth_line(auth_line, sizeof(auth_line));
    static const unsigned char nul = 0;
    if (!auth_length || !write_all_bytes(descriptor, &nul, 1U) ||
        !write_all_bytes(descriptor, auth_line, auth_length)) {
        close(descriptor);
        return -1;
    }
    char reply[96];
    if (!read_line_bytes(descriptor, reply, sizeof(reply)) ||
        strncmp(reply, "OK ", 3) != 0 ||
        !write_all_bytes(descriptor, "BEGIN\r\n", 7U)) {
        close(descriptor);
        errno = EIO;
        return -1;
    }

    unsigned char frame[512];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 1; /* METHOD_CALL */
    frame[3] = 1; /* protocol version */
    unsigned char *fields = frame + 16;
    size_t fields_length = dbus_append_string_field(
        fields, 0, 1, 'o', "/org/freedesktop/DBus");
    fields_length = dbus_append_string_field(
        fields, fields_length, 2, 's', "org.freedesktop.DBus");
    fields_length = dbus_append_string_field(
        fields, fields_length, 3, 's', "Ping");
    fields_length = dbus_append_string_field(
        fields, fields_length, 6, 's', "org.freedesktop.DBus");
    fields_length = dbus_align8(fields_length);
    dbus_write_le32(frame + 4, 0);
    dbus_write_le32(frame + 8, 1);
    dbus_write_le32(frame + 12, (uint32_t)fields_length);
    if (!write_all_bytes(descriptor, frame, 16U + fields_length)) {
        close(descriptor);
        return -1;
    }

    unsigned char response_header[16];
    if (!read_exact_bytes(descriptor, response_header,
                          sizeof(response_header)) ||
        !dbus_byte_order_valid(response_header[0]) ||
        response_header[1] != 2 ||
        dbus_read_order32(response_header + 12, response_header[0]) >
            sizeof(frame) - 16U) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    size_t tail = dbus_align8((size_t)dbus_read_order32(
                      response_header + 12, response_header[0])) +
                  (size_t)dbus_read_order32(response_header + 4,
                                            response_header[0]);
    if (tail > sizeof(frame) - 16U) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    memcpy(frame, response_header, sizeof(response_header));
    if (!read_exact_bytes(descriptor, frame + 16, tail) ||
        akaos_dbus_message_normalize_little_endian(frame, 16U + tail) != 0) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    close(descriptor);
    return 0;
}

static int dbus_binary_open(void) {
    int descriptor = dbus_connect_transport(0);
    if (descriptor < 0) return -1;
    static const unsigned char nul = 0;
    char auth_line[64];
    size_t auth_length = dbus_external_auth_line(auth_line, sizeof(auth_line));
    char reply[96];
    if (!auth_length || !write_all_bytes(descriptor, &nul, 1U) ||
        !write_all_bytes(descriptor, auth_line, auth_length) ||
        !read_line_bytes(descriptor, reply, sizeof(reply)) ||
        strncmp(reply, "OK ", 3) != 0 ||
        !write_all_bytes(descriptor, "BEGIN\r\n", 7U)) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    return descriptor;
}

int akaos_dbus_binary_connect(void) { return dbus_binary_open(); }

int akaos_dbus_async_binary_ping_init(
    struct akaos_dbus_async_binary_ping *ping, int descriptor) {
    if (!ping || descriptor < 0) return -1;
    memset(ping, 0, sizeof(*ping));
    ping->descriptor = descriptor;
    ping->state = AKAOS_DBUS_ASYNC_PENDING;
    unsigned char *frame = ping->request;
    frame[0] = 'l';
    frame[1] = 1; /* METHOD_CALL */
    frame[3] = 1; /* protocol version */
    unsigned char *fields = frame + 16;
    size_t fields_length = dbus_append_string_field(
        fields, 0, 1, 'o', "/org/freedesktop/DBus");
    fields_length = dbus_append_string_field(
        fields, fields_length, 2, 's', "org.freedesktop.DBus");
    fields_length = dbus_append_string_field(
        fields, fields_length, 3, 's', "Ping");
    fields_length = dbus_append_string_field(
        fields, fields_length, 6, 's', "org.freedesktop.DBus");
    fields_length = dbus_align8(fields_length);
    if (fields_length > AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY - 16U) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    dbus_write_le32(frame + 4, 0U);
    dbus_write_le32(frame + 8, 1U);
    dbus_write_le32(frame + 12, (uint32_t)fields_length);
    ping->request_length = 16U + fields_length;
    if (!write_all_bytes(descriptor, frame, ping->request_length)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    return 0;
}

int akaos_dbus_async_binary_ping_pump(
    struct akaos_dbus_async_binary_ping *ping, short revents) {
    if (!ping) return -1;
    if (ping->state == AKAOS_DBUS_ASYNC_DONE) return 1;
    if (ping->state != AKAOS_DBUS_ASYNC_PENDING) return -1;
    if (revents & (POLLERR | POLLNVAL)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    struct pollfd descriptor = {ping->descriptor, POLLIN, 0};
    int ready = poll(&descriptor, 1, 0);
    if (ready < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if (ready == 0) return 0;
    if (descriptor.revents & (POLLERR | POLLNVAL)) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if ((descriptor.revents & POLLIN) == 0) {
        if (descriptor.revents & POLLHUP) {
            ping->state = AKAOS_DBUS_ASYNC_ERROR;
            return -1;
        }
        return 0;
    }
    size_t target = ping->response_received < 16U
        ? 16U : ping->response_expected;
    if (target <= ping->response_received ||
        target > AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    ssize_t received = read(ping->descriptor,
                            ping->response + ping->response_received,
                            target - ping->response_received);
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    if (received == 0) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        return -1;
    }
    ping->response_received += (size_t)received;
    if (ping->response_received < 16U) return 0;
    if (ping->response_expected == 0U) {
        const unsigned char *header = ping->response;
        if (!dbus_byte_order_valid(header[0])) {
            ping->state = AKAOS_DBUS_ASYNC_ERROR;
            errno = EIO;
            return -1;
        }
        uint32_t fields_length = dbus_read_order32(header + 12, header[0]);
        uint32_t body_length = dbus_read_order32(header + 4, header[0]);
        if (header[1] != 2 || header[3] != 1 ||
            fields_length > AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY - 16U) {
            ping->state = AKAOS_DBUS_ASYNC_ERROR;
            errno = EIO;
            return -1;
        }
        size_t tail = dbus_align8((size_t)fields_length);
        if (tail > AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY - 16U ||
            body_length > AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY - 16U -
                               tail) {
            ping->state = AKAOS_DBUS_ASYNC_ERROR;
            errno = EIO;
            return -1;
        }
        tail += body_length;
        ping->response_expected = 16U + tail;
    }
    if (ping->response_received < ping->response_expected) return 0;
    if (akaos_dbus_message_normalize_little_endian(
            ping->response, ping->response_expected) != 0) {
        ping->state = AKAOS_DBUS_ASYNC_ERROR;
        errno = EIO;
        return -1;
    }
    ping->state = AKAOS_DBUS_ASYNC_DONE;
    return 1;
}

int akaos_dbus_async_binary_ping_done(
    const struct akaos_dbus_async_binary_ping *ping) {
    return ping && ping->state == AKAOS_DBUS_ASYNC_DONE;
}

enum {
    AKAOS_DBUS_CLIENT_ACTIVE = 1U,
    AKAOS_DBUS_CLIENT_ERROR = 3U,
    AKAOS_DBUS_REPLY_PENDING = 1U,
    AKAOS_DBUS_REPLY_DONE = 2U,
    AKAOS_DBUS_REPLY_ERROR = 3U,
    AKAOS_DBUS_REPLY_CANCELLED = 4U,
};

static void dbus_async_client_reset_response(
    struct akaos_dbus_async_client *client) {
    if (!client) return;
    dbus_close_fd_array(client->response_fds, &client->response_fd_count);
    client->response_received = 0U;
    client->response_expected = 0U;
}

static void dbus_async_client_clear_slots(
    struct akaos_dbus_async_client *client) {
    if (!client) return;
    for (size_t index = 0; index < AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING;
         ++index) {
        dbus_close_fd_array(client->replies[index].fds,
                            &client->replies[index].fd_count);
        memset(&client->replies[index], 0, sizeof(client->replies[index]));
    }
    dbus_async_client_reset_response(client);
}

static void dbus_async_client_fail(struct akaos_dbus_async_client *client) {
    if (!client) return;
    client->state = AKAOS_DBUS_CLIENT_ERROR;
    dbus_async_client_reset_response(client);
    for (size_t index = 0; index < AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING;
         ++index) {
        struct akaos_dbus_async_reply *reply = &client->replies[index];
        if (reply->used && reply->state == AKAOS_DBUS_REPLY_PENDING) {
            dbus_close_fd_array(reply->fds, &reply->fd_count);
            client->replies[index].state = AKAOS_DBUS_REPLY_ERROR;
        }
    }
}

static struct akaos_dbus_async_reply *dbus_async_client_find_reply(
    struct akaos_dbus_async_client *client, uint32_t serial) {
    if (!client || serial == 0U) return NULL;
    for (size_t index = 0; index < AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING;
         ++index) {
        struct akaos_dbus_async_reply *reply = &client->replies[index];
        if (reply->used && reply->serial == serial) return reply;
    }
    return NULL;
}

/* Decode the bounded header-field representation emitted by the native bus.
 * The field format is deliberately small, but keeping the parser explicit
 * prevents an invalid frame from turning alignment arithmetic into an out of
 * bounds read. */
static int dbus_async_parse_reply_serial(const unsigned char *fields,
                                         size_t fields_length,
                                         uint32_t *serial_out,
                                         uint32_t *fd_count_out,
                                         int *fd_count_found_out) {
    if (!fields || !serial_out || !fd_count_out || !fd_count_found_out)
        return -1;
    int found = 0;
    int fd_count_found = 0;
    uint32_t serial = 0U;
    uint32_t fd_count = 0U;
    size_t offset = 0U;
    while (offset < fields_length) {
        size_t aligned = dbus_align8(offset);
        if (aligned == fields_length) break;
        if (aligned < offset || aligned > fields_length ||
            fields_length - aligned < 4U)
            return -1;
        offset = aligned;
        unsigned char code = fields[offset++];
        unsigned char signature_length = fields[offset++];
        unsigned char type = fields[offset++];
        if (signature_length != 1U || fields[offset++] != 0U) return -1;
        if (type == 'u' || type == 'b') {
            size_t value_offset = dbus_align4(offset);
            if (value_offset < offset || value_offset > fields_length ||
                fields_length - value_offset < 4U)
                return -1;
            if (type == 'u') {
                if (code == 5U) {
                    serial = dbus_read_le32(fields + value_offset);
                    found = 1;
                } else if (code == 9U) {
                    fd_count = dbus_read_le32(fields + value_offset);
                    fd_count_found = 1;
                }
            }
            offset = value_offset + 4U;
        } else if (type == 's' || type == 'o') {
            size_t value_offset = dbus_align4(offset);
            if (value_offset < offset || value_offset > fields_length ||
                fields_length - value_offset < 4U)
                return -1;
            uint32_t length = dbus_read_le32(fields + value_offset);
            size_t remaining = fields_length - value_offset - 4U;
            if ((size_t)length >= remaining) return -1;
            offset = value_offset + 4U + (size_t)length + 1U;
        } else if (type == 'g') {
            if (offset >= fields_length) return -1;
            unsigned char length = fields[offset++];
            if ((size_t)length >= fields_length - offset) return -1;
            offset += (size_t)length + 1U;
        } else {
            return -1;
        }
    }
    *serial_out = serial;
    *fd_count_out = fd_count;
    *fd_count_found_out = fd_count_found;
    return found;
}

int akaos_dbus_async_client_init(struct akaos_dbus_async_client *client,
                                 int descriptor) {
    if (!client || descriptor < 0) return -1;
    memset(client, 0, sizeof(*client));
    client->descriptor = descriptor;
    client->state = AKAOS_DBUS_CLIENT_ACTIVE;
    client->next_serial = 1U;
    return 0;
}

int akaos_dbus_async_client_close(struct akaos_dbus_async_client *client) {
    if (!client) {
        errno = EINVAL;
        return -1;
    }
    int result = 0;
    if (client->descriptor >= 0)
        result = close(client->descriptor);
    client->descriptor = -1;
    client->state = AKAOS_DBUS_CLIENT_ERROR;
    client->next_serial = 0U;
    dbus_async_client_clear_slots(client);
    return result;
}

int akaos_dbus_async_client_reconnect(
    struct akaos_dbus_async_client *client) {
    if (!client || client->descriptor < 0) {
        errno = EINVAL;
        return -1;
    }
    if (akaos_dbus_async_client_close(client) != 0) return -1;
    int descriptor = akaos_dbus_binary_connect();
    if (descriptor < 0) return -1;
    client->descriptor = descriptor;
    client->state = AKAOS_DBUS_CLIENT_ACTIVE;
    client->next_serial = 1U;
    return 0;
}

int akaos_dbus_async_client_retry(struct akaos_dbus_async_client *client,
                                  unsigned max_attempts) {
    if (!client || max_attempts == 0U ||
        max_attempts > AKAOS_DBUS_ASYNC_CLIENT_MAX_RETRIES) {
        errno = EINVAL;
        return -1;
    }
    for (unsigned attempt = 0U; attempt < max_attempts; ++attempt) {
        if (client->descriptor >= 0 &&
            akaos_dbus_async_client_reconnect(client) == 0)
            return 0;
        /* Reconnect deliberately invalidates the old descriptor.  Subsequent
         * bounded attempts start from a clean closed state and authenticate a
         * fresh socket without resurrecting old serials or replies. */
        client->descriptor = -1;
        client->state = AKAOS_DBUS_CLIENT_ERROR;
        dbus_async_client_clear_slots(client);
        int descriptor = akaos_dbus_binary_connect();
        if (descriptor >= 0) {
            client->descriptor = descriptor;
            client->state = AKAOS_DBUS_CLIENT_ACTIVE;
            client->next_serial = 1U;
            return 0;
        }
        if (attempt + 1U < max_attempts) (void)sched_yield();
    }
    return -1;
}

static int dbus_validate_handles_recursive(
    const struct akaos_dbus_value *values, size_t value_count,
    size_t fd_count, unsigned depth) {
    if ((!values && value_count != 0U) || depth > 32U) return 0;
    for (size_t index = 0; index < value_count; ++index) {
        const struct akaos_dbus_value *value = &values[index];
        if (value->kind == AKAOS_DBUS_VALUE_UNIX_FD &&
            value->scalar.uint32_value >= fd_count)
            return 0;
        if (value->child_count != 0U &&
            !dbus_validate_handles_recursive(value->children,
                                             value->child_count, fd_count,
                                             depth + 1U))
            return 0;
    }
    return 1;
}

static int dbus_validate_outgoing_handles(const char *signature,
                                          const unsigned char *body,
                                          size_t body_length,
                                          size_t fd_count) {
    if (!signature || !strchr(signature, 'h')) return 1;
    struct akaos_dbus_value values[64];
    struct akaos_dbus_decode_context context;
    memset(&context, 0, sizeof(context));
    context.values = values;
    context.value_capacity = sizeof(values) / sizeof(values[0]);
    size_t value_count = 0U;
    if (akaos_dbus_unmarshal(signature, body, body_length, &context,
                             &value_count) != 0)
        return 0;
    return dbus_validate_handles_recursive(values, value_count, fd_count, 0U);
}

int akaos_dbus_async_client_queue_call_with_fds(
    struct akaos_dbus_async_client *client, const char *destination,
    const char *path, const char *interface_name, const char *member,
    const char *signature, const unsigned char *body, size_t body_length,
    const int *fds, size_t fd_count, uint32_t *serial_out) {
    if (!client || client->state != AKAOS_DBUS_CLIENT_ACTIVE ||
        !destination || !path || !interface_name || !member ||
        !destination[0] || !path[0] || !interface_name[0] || !member[0] ||
        (body_length != 0U && !body) ||
        (fd_count != 0U && !fds) ||
        fd_count > AKAOS_DBUS_MAX_UNIX_FDS) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < fd_count; ++index) {
        if (fds[index] < 0) {
            errno = EBADF;
            return -1;
        }
    }
    if (strlen(destination) >= AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY ||
        strlen(path) >= AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY ||
        strlen(interface_name) >= AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY ||
        strlen(member) >= AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY ||
        (signature && strlen(signature) >=
                         AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (akaos_dbus_object_path_validate(path) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (signature && signature[0] != '\0') {
        if (akaos_dbus_signature_validate(signature, NULL) != 0 ||
            akaos_dbus_body_validate(signature, body, body_length, NULL) != 0)
            return -1;
    } else if (body_length != 0U) {
        errno = EINVAL;
        return -1;
    }
    if (!dbus_validate_outgoing_handles(signature, body, body_length,
                                        fd_count)) {
        errno = EBADF;
        return -1;
    }
    struct akaos_dbus_async_reply *slot = NULL;
    for (size_t index = 0; index < AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING;
         ++index) {
        if (!client->replies[index].used) {
            slot = &client->replies[index];
            break;
        }
    }
    if (!slot) {
        errno = EAGAIN;
        return -1;
    }
    unsigned char frame[AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 1U; /* METHOD_CALL */
    frame[3] = 1U; /* protocol version */
    unsigned char *fields = frame + 16;
    size_t fields_length = dbus_append_string_field(
        fields, 0U, 1U, 'o', path);
    fields_length = dbus_append_string_field(
        fields, fields_length, 2U, 's', interface_name);
    fields_length = dbus_append_string_field(
        fields, fields_length, 3U, 's', member);
    fields_length = dbus_append_string_field(
        fields, fields_length, 6U, 's', destination);
    if (fd_count != 0U)
        fields_length = dbus_append_uint32_field(
            fields, fields_length, 9U, (uint32_t)fd_count);
    if (signature && signature[0] != '\0')
        fields_length = dbus_append_signature_field(fields, fields_length,
                                                    signature);
    fields_length = dbus_align8(fields_length);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length) {
        errno = EMSGSIZE;
        return -1;
    }
    if (body_length) memcpy(frame + 16U + fields_length, body, body_length);
    uint32_t serial = client->next_serial++;
    if (serial == 0U) serial = client->next_serial++;
    dbus_write_le32(frame + 4U, (uint32_t)body_length);
    dbus_write_le32(frame + 8U, serial);
    dbus_write_le32(frame + 12U, (uint32_t)fields_length);
    size_t frame_length = 16U + fields_length + body_length;
    if (!dbus_send_bytes_with_fds(client->descriptor, frame, frame_length,
                                  fds, fd_count)) {
        dbus_async_client_fail(client);
        errno = EIO;
        return -1;
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = 1U;
    slot->serial = serial;
    slot->state = AKAOS_DBUS_REPLY_PENDING;
    if (serial_out) *serial_out = serial;
    return 0;
}

int akaos_dbus_async_client_queue_call(
    struct akaos_dbus_async_client *client, const char *destination,
    const char *path, const char *interface_name, const char *member,
    const char *signature, const unsigned char *body, size_t body_length,
    uint32_t *serial_out) {
    return akaos_dbus_async_client_queue_call_with_fds(
        client, destination, path, interface_name, member, signature, body,
        body_length, NULL, 0U, serial_out);
}

size_t akaos_dbus_async_client_pending(
    const struct akaos_dbus_async_client *client) {
    if (!client) return 0U;
    size_t count = 0U;
    for (size_t index = 0; index < AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING;
         ++index) {
        if (client->replies[index].used &&
            client->replies[index].state == AKAOS_DBUS_REPLY_PENDING)
            ++count;
    }
    return count;
}

int akaos_dbus_async_client_pump(struct akaos_dbus_async_client *client,
                                 short revents) {
    if (!client || client->state != AKAOS_DBUS_CLIENT_ACTIVE) return -1;
    if (revents & (POLLERR | POLLNVAL)) {
        dbus_async_client_fail(client);
        return -1;
    }
    struct pollfd descriptor = {client->descriptor, POLLIN, 0};
    int ready = poll(&descriptor, 1, 0);
    if (ready < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        dbus_async_client_fail(client);
        return -1;
    }
    if (ready == 0) return 0;
    if (descriptor.revents & (POLLERR | POLLNVAL)) {
        dbus_async_client_fail(client);
        return -1;
    }
    if ((descriptor.revents & POLLIN) == 0) {
        if (descriptor.revents & POLLHUP) {
            dbus_async_client_fail(client);
            return -1;
        }
        return 0;
    }
    size_t target = client->response_received < 16U
        ? 16U : client->response_expected;
    if (target <= client->response_received ||
        target > AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY) {
        dbus_async_client_fail(client);
        errno = EIO;
        return -1;
    }
    size_t received = 0U;
    if (!dbus_receive_chunk(client->descriptor,
                            client->response + client->response_received,
                            target - client->response_received, &received,
                            client->response_fds,
                            &client->response_fd_count,
                            client->response_received == 0U)) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        dbus_async_client_fail(client);
        errno = EIO;
        return -1;
    }
    client->response_received += received;
    if (client->response_received < 16U) return 0;
    if (client->response_expected == 0U) {
        unsigned char type = client->response[1];
        if (!dbus_byte_order_valid(client->response[0])) {
            dbus_async_client_fail(client);
            errno = EIO;
            return -1;
        }
        uint32_t fields_length = dbus_read_order32(
            client->response + 12U, client->response[0]);
        uint32_t body_length = dbus_read_order32(
            client->response + 4U, client->response[0]);
        if (client->response[3] != 1U ||
            (type != 2U && type != 3U && type != 4U) ||
            fields_length > AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY - 16U) {
            dbus_async_client_fail(client);
            errno = EIO;
            return -1;
        }
        size_t tail = dbus_align8((size_t)fields_length);
        if (tail > AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY - 16U ||
            body_length > AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY - 16U -
                               tail) {
            dbus_async_client_fail(client);
            errno = EOVERFLOW;
            return -1;
        }
        client->response_expected = 16U + tail + body_length;
    }
    if (client->response_received < client->response_expected) return 0;
    if (akaos_dbus_message_normalize_little_endian(
            client->response, client->response_expected) != 0) {
        dbus_async_client_fail(client);
        errno = EIO;
        return -1;
    }

    unsigned char type = client->response[1];
    uint32_t fields_length = dbus_read_le32(client->response + 12U);
    uint32_t body_length = dbus_read_le32(client->response + 4U);
    uint32_t reply_serial = 0U;
    uint32_t declared_fd_count = 0U;
    int fd_count_found = 0;
    int parsed = dbus_async_parse_reply_serial(
        client->response + 16U, fields_length, &reply_serial,
        &declared_fd_count, &fd_count_found);
    if (parsed < 0 ||
        (client->response_fd_count != 0U && !fd_count_found) ||
        (fd_count_found &&
         declared_fd_count != client->response_fd_count)) {
        dbus_async_client_fail(client);
        errno = EIO;
        return -1;
    }
    if (type != 4U) {
        if (parsed < 0 || (parsed == 0 && type != 4U)) {
            dbus_async_client_fail(client);
            errno = EIO;
            return -1;
        }
        struct akaos_dbus_async_reply *slot =
            dbus_async_client_find_reply(client, reply_serial);
        if (slot) {
            slot->body_length = body_length;
            if (body_length > AKAOS_DBUS_ASYNC_CLIENT_REPLY_CAPACITY) {
                slot->state = AKAOS_DBUS_REPLY_ERROR;
                slot->body_length = 0U;
                dbus_close_fd_array(client->response_fds,
                                    &client->response_fd_count);
            } else {
                size_t body_offset = 16U + dbus_align8(fields_length);
                if (body_length)
                    memcpy(slot->body, client->response + body_offset,
                           body_length);
                slot->fd_count = client->response_fd_count;
                for (size_t index = 0; index < slot->fd_count; ++index) {
                    slot->fds[index] = client->response_fds[index];
                    client->response_fds[index] = -1;
                }
                client->response_fd_count = 0U;
                slot->state = type == 3U ? AKAOS_DBUS_REPLY_ERROR
                                         : AKAOS_DBUS_REPLY_DONE;
                if (type == 3U)
                    dbus_close_fd_array(slot->fds, &slot->fd_count);
            }
        }
    }
    dbus_async_client_reset_response(client);
    return 1;
}

static int dbus_async_client_take_reply(
    struct akaos_dbus_async_client *client, uint32_t serial, void *body,
    size_t body_capacity, size_t *body_length, int *fds, size_t fd_capacity,
    size_t *fd_count, int *is_error, int discard_fds) {
    struct akaos_dbus_async_reply *slot =
        dbus_async_client_find_reply(client, serial);
    if (!slot) {
        errno = ENOENT;
        return -1;
    }
    if (slot->state == AKAOS_DBUS_REPLY_PENDING) {
        errno = EAGAIN;
        return 0;
    }
    if (slot->state == AKAOS_DBUS_REPLY_ERROR) {
        if (is_error) *is_error = 1;
        dbus_close_fd_array(slot->fds, &slot->fd_count);
        slot->used = 0U;
        errno = EIO;
        return -1;
    }
    if (slot->state == AKAOS_DBUS_REPLY_CANCELLED) {
        if (body_length) *body_length = 0U;
        if (is_error) *is_error = 0;
        dbus_close_fd_array(slot->fds, &slot->fd_count);
        slot->used = 0U;
        errno = ECANCELED;
        return -1;
    }
    if (!discard_fds && slot->fd_count != 0U &&
        (!fds || fd_capacity < slot->fd_count)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (slot->body_length > body_capacity ||
        (slot->body_length != 0U && !body)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (slot->body_length) memcpy(body, slot->body, slot->body_length);
    if (body_length) *body_length = slot->body_length;
    if (is_error) *is_error = 0;
    if (fd_count) *fd_count = slot->fd_count;
    if (discard_fds) {
        dbus_close_fd_array(slot->fds, &slot->fd_count);
    } else {
        for (size_t index = 0; index < slot->fd_count; ++index) {
            fds[index] = slot->fds[index];
            slot->fds[index] = -1;
        }
        slot->fd_count = 0U;
    }
    slot->used = 0U;
    return 1;
}

int akaos_dbus_async_client_take_reply(struct akaos_dbus_async_client *client,
                                       uint32_t serial, void *body,
                                       size_t body_capacity,
                                       size_t *body_length, int *is_error) {
    return dbus_async_client_take_reply(client, serial, body, body_capacity,
                                        body_length, NULL, 0U, NULL, is_error,
                                        1);
}

int akaos_dbus_async_client_take_reply_with_fds(
    struct akaos_dbus_async_client *client, uint32_t serial, void *body,
    size_t body_capacity, size_t *body_length, int *fds,
    size_t fd_capacity, size_t *fd_count, int *is_error) {
    return dbus_async_client_take_reply(client, serial, body, body_capacity,
                                        body_length, fds, fd_capacity,
                                        fd_count, is_error, 0);
}

int akaos_dbus_async_client_cancel(struct akaos_dbus_async_client *client,
                                   uint32_t serial) {
    struct akaos_dbus_async_reply *slot =
        dbus_async_client_find_reply(client, serial);
    if (!slot) {
        errno = ENOENT;
        return -1;
    }
    if (slot->state != AKAOS_DBUS_REPLY_PENDING) {
        errno = EINVAL;
        return -1;
    }
    slot->state = AKAOS_DBUS_REPLY_CANCELLED;
    slot->body_length = 0U;
    return 1;
}

static int dbus_binary_call_to_endpoint(
    int descriptor, uint32_t serial, const char *destination,
    const char *path, const char *interface_name, const char *member,
    const char *signature, const unsigned char *body, size_t body_length,
    unsigned char *response_body, size_t response_capacity,
    size_t *response_length) {
    /* Standard broker introspection is deliberately descriptive and can be
     * larger than the compact desktop-service replies.  Keep the transport
     * bounded, but large enough for a complete XML reply. */
    unsigned char frame[4096];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 1; /* METHOD_CALL */
    frame[3] = 1; /* protocol version */
    if (!destination || !path || !interface_name || !member ||
        destination[0] == '\0' || path[0] != '/' || interface_name[0] == '\0' ||
        member[0] == '\0')
        return 0;
    if (akaos_dbus_object_path_validate(path) != 0 ||
        (signature && signature[0] != '\0' &&
         (akaos_dbus_signature_validate(signature, NULL) != 0 ||
          akaos_dbus_body_validate(signature, body, body_length, NULL) != 0)) ||
        ((!signature || signature[0] == '\0') && body_length != 0U))
        return 0;
    unsigned char *fields = frame + 16;
    size_t fields_length = dbus_append_string_field(
        fields, 0, 1, 'o', path);
    fields_length = dbus_append_string_field(
        fields, fields_length, 2, 's', interface_name);
    fields_length = dbus_append_string_field(
        fields, fields_length, 3, 's', member);
    fields_length = dbus_append_string_field(
        fields, fields_length, 6, 's',
        destination ? destination : "org.freedesktop.DBus");
    if (signature && signature[0] != '\0')
        fields_length = dbus_append_signature_field(fields, fields_length,
                                                    signature);
    fields_length = dbus_align8(fields_length);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length) return 0;
    if (body_length > 0) memcpy(frame + 16 + fields_length, body, body_length);
    dbus_write_le32(frame + 4, (uint32_t)body_length);
    dbus_write_le32(frame + 8, serial);
    dbus_write_le32(frame + 12, (uint32_t)fields_length);
    if (!write_all_bytes(descriptor, frame, 16U + fields_length + body_length))
        return 0;

    unsigned char response_header[16];
    if (!read_exact_bytes(descriptor, response_header,
                          sizeof(response_header)) ||
        !dbus_byte_order_valid(response_header[0]) ||
        response_header[1] != 2 ||
        dbus_read_order32(response_header + 12, response_header[0]) >
            sizeof(frame) - 16U)
        return 0;
    size_t response_fields = (size_t)dbus_read_order32(
        response_header + 12, response_header[0]);
    size_t response_tail = dbus_align8(response_fields) +
                           (size_t)dbus_read_order32(
                               response_header + 4, response_header[0]);
    if (response_tail > sizeof(frame) - 16U ||
        !read_exact_bytes(descriptor, frame + 16, response_tail)) return 0;
    memcpy(frame, response_header, sizeof(response_header));
    if (akaos_dbus_message_normalize_little_endian(
            frame, 16U + response_tail) != 0)
        return 0;
    response_fields = (size_t)dbus_read_le32(frame + 12U);
    size_t length = (size_t)dbus_read_le32(frame + 4U);
    if (length > response_capacity) return 0;
    if (length > 0 && response_body)
        memcpy(response_body, frame + 16 + dbus_align8(response_fields), length);
    if (response_length) *response_length = length;
    return 1;
}

static int dbus_binary_call_to(int descriptor, uint32_t serial,
                            const char *destination, const char *member,
                            const char *signature,
                            const unsigned char *body, size_t body_length,
                            unsigned char *response_body,
                            size_t response_capacity, size_t *response_length) {
    return dbus_binary_call_to_endpoint(
        descriptor, serial, destination, "/org/freedesktop/DBus",
        "org.freedesktop.DBus", member, signature, body, body_length,
        response_body, response_capacity, response_length);
}

static int dbus_binary_call(int descriptor, uint32_t serial,
                            const char *member, const char *signature,
                            const unsigned char *body, size_t body_length,
                            unsigned char *response_body,
                            size_t response_capacity, size_t *response_length) {
    return dbus_binary_call_to(descriptor, serial, "org.freedesktop.DBus",
                               member, signature, body, body_length,
                               response_body, response_capacity,
                               response_length);
}

static int dbus_body_unique_name(const unsigned char *body, size_t length) {
    if (length < 6U) return 0;
    uint32_t string_length = dbus_read_le32(body);
    return string_length >= 3U && string_length < 32U &&
           (size_t)string_length + 5U == length && body[4] == ':' &&
           body[4U + string_length] == 0;
}

static int dbus_body_uint_equals(const unsigned char *body, size_t length,
                                 uint32_t expected) {
    return length == 4U && dbus_read_le32(body) == expected;
}

static int dbus_body_empty(const unsigned char *body, size_t length) {
    (void)body;
    return length == 0;
}

static int dbus_body_three_strings(const unsigned char *body, size_t length,
                                   char *first, size_t first_capacity,
                                   char *second, size_t second_capacity,
                                   char *third, size_t third_capacity) {
    char *values[3] = {first, second, third};
    size_t capacities[3] = {first_capacity, second_capacity, third_capacity};
    size_t offset = 0;
    for (unsigned index = 0; index < 3U; ++index) {
        offset = dbus_align4(offset);
        if (offset + 4U > length || !values[index] || capacities[index] == 0)
            return 0;
        uint32_t string_length = dbus_read_le32(body + offset);
        offset += 4U;
        if ((size_t)string_length + 1U > length - offset ||
            (size_t)string_length >= capacities[index] ||
            body[offset + string_length] != 0)
            return 0;
        memcpy(values[index], body + offset, string_length);
        values[index][string_length] = '\0';
        offset += (size_t)string_length + 1U;
    }
    return offset == length;
}

static int dbus_registration_result(uint32_t result) {
    switch (result) {
        case 1U:
            return 0;
        case 2U:
            errno = EEXIST;
            break;
        case 3U:
            errno = EINVAL;
            break;
        case 4U:
            errno = EIO;
            break;
        case 5U:
            errno = EBUSY;
            break;
        default:
            errno = EIO;
            break;
    }
    return -1;
}

int akaos_dbus_register_service(const char *name, const char *path,
                                const char *argument, int restart,
                                uint32_t timeout_ms) {
    if (!name || !path || path[0] != '/' || !argument ||
        strlen(name) >= 64U || strlen(path) >= 96U ||
        strlen(argument) >= 96U) {
        errno = EINVAL;
        return -1;
    }
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char request[320];
    size_t request_length = 0;
    const char *values[] = {name, path, argument};
    for (unsigned index = 0; index < 3U; ++index) {
        request_length = dbus_align4(request_length);
        size_t value_length = strlen(values[index]);
        if (request_length + value_length + 5U > sizeof(request)) {
            close(descriptor);
            errno = EINVAL;
            return -1;
        }
        dbus_write_le32(request + request_length, (uint32_t)value_length);
        request_length += 4U;
        memcpy(request + request_length, values[index], value_length + 1U);
        request_length += value_length + 1U;
    }
    request_length = dbus_align4(request_length);
    if (request_length + 8U > sizeof(request)) {
        close(descriptor);
        errno = EINVAL;
        return -1;
    }
    dbus_write_le32(request + request_length, restart ? 1U : 0U);
    dbus_write_le32(request + request_length + 4U, timeout_ms);
    request_length += 8U;
    unsigned char body[64];
    size_t body_length = 0;
    int sent = dbus_binary_call_to(
        descriptor, 1, "org.freedesktop.DBus", "RegisterService", "sssuu",
        request, request_length, body, sizeof(body), &body_length);
    uint32_t result = sent && body_length == 4U ? dbus_read_le32(body) : 4U;
    close(descriptor);
    return dbus_registration_result(result);
}

int akaos_dbus_unregister_service(const char *name) {
    if (!name || strlen(name) >= 64U) {
        errno = EINVAL;
        return -1;
    }
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    size_t length = strlen(name);
    unsigned char request[68];
    dbus_write_le32(request, (uint32_t)length);
    memcpy(request + 4U, name, length + 1U);
    unsigned char body[64];
    size_t body_length = 0;
    int sent = dbus_binary_call_to(
        descriptor, 1, "org.freedesktop.DBus", "UnregisterService", "s",
        request, length + 5U, body, sizeof(body), &body_length);
    uint32_t result = sent && body_length == 4U ? dbus_read_le32(body) : 4U;
    close(descriptor);
    return dbus_registration_result(result);
}

int akaos_dbus_claim_name(const char *name) {
    if (!name || name[0] == '\0' || strlen(name) >= 64U) {
        errno = EINVAL;
        return -1;
    }
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char body[128];
    size_t body_length = 0;
    unsigned char request[68];
    size_t length = strlen(name);
    dbus_write_le32(request, (uint32_t)length);
    memcpy(request + 4U, name, length + 1U);
    if (!dbus_binary_call(descriptor, 1U, "Hello", NULL, NULL, 0U,
                          body, sizeof(body), &body_length) ||
        !dbus_binary_call(descriptor, 2U, "RequestName", "s", request,
                          length + 5U, body, sizeof(body), &body_length)) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    uint32_t result = body_length == 4U ? dbus_read_le32(body) : 0U;
    if (result != 1U) {
        close(descriptor);
        errno = result == 3U ? EBUSY : EEXIST;
        return -1;
    }
    return descriptor;
}

int akaos_dbus_release_name(int descriptor, const char *name) {
    if (descriptor < 0 || !name || name[0] == '\0' ||
        strlen(name) >= 64U) {
        errno = EINVAL;
        if (descriptor >= 0) close(descriptor);
        return -1;
    }
    unsigned char body[64];
    size_t body_length = 0;
    unsigned char request[68];
    size_t length = strlen(name);
    dbus_write_le32(request, (uint32_t)length);
    memcpy(request + 4U, name, length + 1U);
    int sent = dbus_binary_call(descriptor, 3U, "ReleaseName", "s", request,
                                length + 5U, body, sizeof(body),
                                &body_length);
    uint32_t result = sent && body_length == 4U ? dbus_read_le32(body) : 0U;
    close(descriptor);
    if (result != 1U) {
        errno = result == 2U ? EACCES : EIO;
        return -1;
    }
    return 0;
}

static int dbus_read_frame(int descriptor, unsigned char *frame,
                           size_t capacity, size_t *length_out) {
    if (!frame || capacity < 16U || !length_out) return 0;
    unsigned char header[16];
    if (!read_exact_bytes(descriptor, header, sizeof(header)) ||
        !dbus_byte_order_valid(header[0])) return 0;
    size_t fields = (size_t)dbus_read_order32(header + 12, header[0]);
    size_t body = (size_t)dbus_read_order32(header + 4, header[0]);
    size_t tail = dbus_align8(fields) + body;
    if (fields > capacity - 16U || tail > capacity - 16U) return 0;
    memcpy(frame, header, sizeof(header));
    if (!read_exact_bytes(descriptor, frame + 16, tail)) return 0;
    *length_out = 16U + tail;
    return akaos_dbus_message_normalize_little_endian(
        frame, *length_out) == 0;
}

static int dbus_read_frame_with_fds(int descriptor, unsigned char *frame,
                                    size_t capacity, size_t *length_out,
                                    int *fds, size_t *fd_count) {
    if (!frame || capacity < 16U || !length_out || !fds || !fd_count)
        return 0;
    *length_out = 0U;
    *fd_count = 0U;
    for (size_t index = 0; index < AKAOS_DBUS_MAX_UNIX_FDS; ++index)
        fds[index] = -1;
    size_t received = 0U;
    int chunk_received;
    do {
        chunk_received = dbus_receive_chunk(descriptor, frame, 16U,
                                            &received, fds, fd_count, 1);
        if (!chunk_received && (errno == EAGAIN || errno == EINTR))
            (void)sched_yield();
    } while (!chunk_received && (errno == EAGAIN || errno == EINTR));
    if (!chunk_received) return 0;
    if (received < 16U &&
        !read_exact_bytes(descriptor, frame + received, 16U - received))
        goto fail;
    if (!dbus_byte_order_valid(frame[0])) goto fail;
    size_t fields_length =
        (size_t)dbus_read_order32(frame + 12U, frame[0]);
    size_t body_length = (size_t)dbus_read_order32(frame + 4U, frame[0]);
    if (fields_length > capacity - 16U) goto fail;
    size_t padded_fields = dbus_align8(fields_length);
    if (padded_fields > capacity - 16U ||
        body_length > capacity - 16U - padded_fields)
        goto fail;
    size_t tail = padded_fields + body_length;
    if (!read_exact_bytes(descriptor, frame + 16U, tail)) goto fail;
    *length_out = 16U + tail;
    if (akaos_dbus_message_normalize_little_endian(frame, *length_out) != 0)
        goto fail;
    uint32_t ignored_serial = 0U;
    uint32_t declared_count = 0U;
    int count_found = 0;
    int parsed = dbus_async_parse_reply_serial(
        frame + 16U, dbus_read_le32(frame + 12U), &ignored_serial,
        &declared_count, &count_found);
    if (parsed < 0 || (count_found ? declared_count : 0U) != *fd_count ||
        *fd_count > AKAOS_DBUS_MAX_UNIX_FDS)
        goto fail;
    return 1;
fail:
    dbus_close_fd_array(fds, fd_count);
    *length_out = 0U;
    return 0;
}

static int dbus_send_big_endian_test_call(
    int descriptor, uint32_t serial, const char *member,
    const char *signature, const unsigned char *body, size_t body_length) {
    unsigned char frame[512];
    memset(frame, 0, sizeof(frame));
    frame[0] = AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
    frame[1] = 1U;
    frame[3] = 1U;
    unsigned char *fields = frame + 16U;
    size_t fields_length = dbus_append_string_field_order(
        fields, 0U, 1U, 'o', "/org/freedesktop/DBus",
        AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    fields_length = dbus_append_string_field_order(
        fields, fields_length, 2U, 's', "org.freedesktop.DBus",
        AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    fields_length = dbus_append_string_field_order(
        fields, fields_length, 3U, 's', member,
        AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    fields_length = dbus_append_string_field_order(
        fields, fields_length, 6U, 's', "org.freedesktop.DBus",
        AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    if (signature && signature[0] != '\0')
        fields_length = dbus_append_signature_field(fields, fields_length,
                                                    signature);
    fields_length = dbus_align8(fields_length);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length ||
        (body_length != 0U && !body))
        return 0;
    if (body_length != 0U)
        memcpy(frame + 16U + fields_length, body, body_length);
    dbus_write_order32(frame + 4U, (uint32_t)body_length,
                       AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    dbus_write_order32(frame + 8U, serial,
                       AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    dbus_write_order32(frame + 12U, (uint32_t)fields_length,
                       AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    return write_all_bytes(descriptor, frame,
                           16U + fields_length + body_length);
}

int akaos_dbus_binary_byte_order_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char frame[1024];
    size_t frame_length = 0U;
    int success = dbus_send_big_endian_test_call(
                      descriptor, 1U, "Hello", "", NULL, 0U) &&
                  dbus_read_frame(descriptor, frame, sizeof(frame),
                                  &frame_length) &&
                  frame_length >= 16U && frame[1] == 2U;
    const char name[] = "org.akaos.BigEndianProbe";
    unsigned char string_body[sizeof(name) + 4U];
    dbus_write_order32(string_body, (uint32_t)(sizeof(name) - 1U),
                       AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN);
    memcpy(string_body + 4U, name, sizeof(name));
    if (success) {
        success = dbus_send_big_endian_test_call(
                      descriptor, 2U, "RequestName", "s", string_body,
                      sizeof(string_body)) &&
                  dbus_read_frame(descriptor, frame, sizeof(frame),
                                  &frame_length) &&
                  frame_length >= 16U && frame[1] == 2U;
    }
    if (success) {
        size_t fields_length = (size_t)dbus_read_le32(frame + 12U);
        size_t body_length = (size_t)dbus_read_le32(frame + 4U);
        size_t body_offset = 16U + dbus_align8(fields_length);
        success = body_length == 4U && body_offset <= frame_length &&
                  body_length <= frame_length - body_offset &&
                  dbus_read_le32(frame + body_offset) == 1U;
    }
    if (success) {
        success = dbus_send_big_endian_test_call(
                      descriptor, 3U, "ReleaseName", "s", string_body,
                      sizeof(string_body)) &&
                  dbus_read_frame(descriptor, frame, sizeof(frame),
                                  &frame_length) &&
                  frame_length >= 16U && frame[1] == 2U;
    }
    if (success) {
        size_t fields_length = (size_t)dbus_read_le32(frame + 12U);
        size_t body_length = (size_t)dbus_read_le32(frame + 4U);
        size_t body_offset = 16U + dbus_align8(fields_length);
        success = body_length == 4U && body_offset <= frame_length &&
                  body_length <= frame_length - body_offset &&
                  dbus_read_le32(frame + body_offset) == 1U;
    }
    close(descriptor);
    if (!success) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

/* Read one string-valued D-Bus header field.  The broker and resident
 * desktop components share the same bounded little-endian wire format, but
 * the service side must still inspect the member instead of replying with a
 * success body to every method. */
static int dbus_find_header_string(const unsigned char *fields,
                                   size_t fields_length, unsigned char wanted,
                                   char *value, size_t capacity) {
    if (!fields || !value || capacity == 0) return 0;
    size_t offset = 0;
    while (offset < fields_length) {
        size_t cursor = dbus_align8(offset);
        if (cursor + 4U > fields_length) return 0;
        unsigned char code = fields[cursor++];
        unsigned char signature_length = fields[cursor++];
        if (signature_length == 0U || signature_length > 8U ||
            cursor + (size_t)signature_length + 1U > fields_length)
            return 0;
        char signature = (char)fields[cursor];
        cursor += (size_t)signature_length + 1U;
        if (signature == 's' || signature == 'o') {
            cursor = dbus_align4(cursor);
            if (cursor + 4U > fields_length) return 0;
            uint32_t length = dbus_read_le32(fields + cursor);
            cursor += 4U;
            if ((size_t)length + 1U > fields_length - cursor ||
                (size_t)length >= capacity || fields[cursor + length] != 0)
                return 0;
            if (code == wanted) {
                memcpy(value, fields + cursor, length);
                value[length] = '\0';
                return 1;
            }
            offset = cursor + (size_t)length + 1U;
            continue;
        }
        if (signature == 'g') {
            if (cursor >= fields_length) return 0;
            unsigned char length = fields[cursor++];
            if ((size_t)length + 1U > fields_length - cursor) return 0;
            offset = cursor + (size_t)length + 1U;
            continue;
        }
        if (signature == 'u' || signature == 'b') {
            cursor = dbus_align4(cursor);
            if (cursor + 4U > fields_length) return 0;
            offset = cursor + 4U;
            continue;
        }
        if (signature == 'y') {
            if (cursor >= fields_length) return 0;
            offset = cursor + 1U;
            continue;
        }
        return 0;
    }
    return 0;
}

/* Header field 8 is a D-Bus signature (a one-byte length followed by the
 * signature bytes and a NUL).  Keep this separate from string/object-path
 * parsing so routed desktop services can validate typed calls without
 * accepting an arbitrary body under the wrong method signature. */
static int dbus_find_header_signature(const unsigned char *fields,
                                      size_t fields_length,
                                      char *value, size_t capacity) {
    if (!fields || !value || capacity == 0U) return 0;
    size_t offset = 0;
    while (offset < fields_length) {
        size_t cursor = dbus_align8(offset);
        if (cursor + 4U > fields_length) return 0;
        unsigned char code = fields[cursor++];
        unsigned char signature_length = fields[cursor++];
        if (signature_length == 0U || signature_length > 32U ||
            cursor + (size_t)signature_length + 1U > fields_length)
            return 0;
        char variant = (char)fields[cursor];
        cursor += (size_t)signature_length + 1U;
        if (code == 8U && variant == 'g' &&
            (size_t)signature_length < capacity) {
            if (cursor > fields_length || fields[cursor - 1U] != 0U)
                return 0;
            /* A header signature field stores its value as a one-byte
             * length, signature bytes, and NUL after the variant type. */
            if (cursor >= fields_length) return 0;
            unsigned char value_length = fields[cursor++];
            if ((size_t)value_length + 1U > fields_length - cursor ||
                (size_t)value_length >= capacity ||
                fields[cursor + value_length] != 0U)
                return 0;
            memcpy(value, fields + cursor, value_length);
            value[value_length] = '\0';
            return 1;
        }
        if (variant == 'g') {
            if (cursor >= fields_length) return 0;
            unsigned char value_length = fields[cursor++];
            if ((size_t)value_length + 1U > fields_length - cursor) return 0;
            cursor += (size_t)value_length + 1U;
        } else if (variant == 's' || variant == 'o') {
            cursor = dbus_align4(cursor);
            if (cursor + 4U > fields_length) return 0;
            uint32_t length = dbus_read_le32(fields + cursor);
            cursor += 4U;
            if ((size_t)length + 1U > fields_length - cursor) return 0;
            cursor += (size_t)length + 1U;
        } else if (variant == 'u' || variant == 'b') {
            cursor = dbus_align4(cursor);
            if (cursor + 4U > fields_length) return 0;
            cursor += 4U;
        } else if (variant == 'y') {
            if (cursor >= fields_length) return 0;
            ++cursor;
        } else {
            return 0;
        }
        offset = cursor;
    }
    return 0;
}

static int dbus_body_string_contains(const unsigned char *body, size_t length,
                                     const char *needle) {
    if (!body || length < 5U || !needle) return 0;
    uint32_t string_length = dbus_read_le32(body);
    if ((size_t)string_length + 5U != length ||
        body[4U + string_length] != '\0') return 0;
    size_t needle_length = strlen(needle);
    if (needle_length == 0U || needle_length > string_length) return 0;
    for (size_t offset = 0; offset + needle_length <= string_length; ++offset)
        if (memcmp(body + 4U + offset, needle, needle_length) == 0) return 1;
    return 0;
}

static int dbus_send_error_frame(int descriptor, uint32_t serial,
                                 const char *error_name,
                                 const char *message) {
    if (descriptor < 0 || !error_name || !message) return 0;
    unsigned char frame[1024];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 3; /* ERROR */
    frame[3] = 1;
    size_t fields = dbus_append_uint32_field(frame + 16, 0, 5, serial);
    fields = dbus_append_string_field(frame + 16, fields, 4, 's', error_name);
    fields = dbus_append_signature_field(frame + 16, fields, "s");
    size_t fields_length = dbus_align8(fields);
    size_t message_length = strlen(message);
    if (fields_length > sizeof(frame) - 16U - 5U ||
        message_length >= sizeof(frame) - 16U - fields_length - 5U)
        return 0;
    unsigned char *body = frame + 16 + fields_length;
    dbus_write_le32(body, (uint32_t)message_length);
    memcpy(body + 4U, message, message_length + 1U);
    dbus_write_le32(frame + 4, (uint32_t)(message_length + 5U));
    dbus_write_le32(frame + 8, 1U);
    dbus_write_le32(frame + 12, (uint32_t)fields_length);
    return write_all_bytes(descriptor, frame,
                           16U + fields_length + message_length + 5U);
}

static int dbus_send_method_return_frame(int descriptor, uint32_t serial,
                                         const char *signature,
                                         const unsigned char *body,
                                         size_t body_length) {
    unsigned char frame[1024];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 2;
    frame[3] = 1;
    size_t fields = dbus_append_uint32_field(frame + 16, 0, 5, serial);
    if (signature && signature[0] != '\0')
        fields = dbus_append_signature_field(frame + 16, fields, signature);
    size_t fields_length = dbus_align8(fields);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length) return 0;
    if (body_length > 0) memcpy(frame + 16 + fields_length, body, body_length);
    dbus_write_le32(frame + 4, (uint32_t)body_length);
    dbus_write_le32(frame + 8, serial);
    dbus_write_le32(frame + 12, (uint32_t)fields_length);
    return write_all_bytes(descriptor, frame, 16U + fields_length + body_length);
}

static int dbus_send_method_return_frame_with_fds(
    int descriptor, uint32_t serial, const char *signature,
    const unsigned char *body, size_t body_length, const int *fds,
    size_t fd_count) {
    if (fd_count == 0U || fd_count > AKAOS_DBUS_MAX_UNIX_FDS || !fds ||
        (signature && signature[0] != '\0' &&
         (akaos_dbus_signature_validate(signature, NULL) != 0 ||
          akaos_dbus_body_validate(signature, body, body_length, NULL) != 0)) ||
        !dbus_validate_outgoing_handles(signature, body, body_length,
                                        fd_count)) {
        errno = EINVAL;
        return 0;
    }
    unsigned char frame[1024];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 2U;
    frame[3] = 1U;
    size_t fields = dbus_append_uint32_field(frame + 16U, 0U, 5U, serial);
    fields = dbus_append_uint32_field(frame + 16U, fields, 9U,
                                      (uint32_t)fd_count);
    if (signature && signature[0] != '\0')
        fields = dbus_append_signature_field(frame + 16U, fields, signature);
    size_t fields_length = dbus_align8(fields);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length)
        return 0;
    if (body_length != 0U)
        memcpy(frame + 16U + fields_length, body, body_length);
    dbus_write_le32(frame + 4U, (uint32_t)body_length);
    dbus_write_le32(frame + 8U, serial);
    dbus_write_le32(frame + 12U, (uint32_t)fields_length);
    return dbus_send_bytes_with_fds(descriptor, frame,
                                    16U + fields_length + body_length,
                                    fds, fd_count);
}

static int dbus_body_string_at(const unsigned char *body, size_t length,
                               size_t *offset, char *value, size_t capacity) {
    if (!body || !offset || !value || capacity == 0U || *offset > length)
        return 0;
    size_t cursor = dbus_align4(*offset);
    if (cursor + 5U > length) return 0;
    uint32_t string_length = dbus_read_le32(body + cursor);
    cursor += 4U;
    if ((size_t)string_length + 1U > length - cursor ||
        (size_t)string_length >= capacity ||
        body[cursor + string_length] != 0U)
        return 0;
    memcpy(value, body + cursor, string_length);
    value[string_length] = '\0';
    *offset = cursor + (size_t)string_length + 1U;
    return 1;
}

static int dbus_body_two_strings(const unsigned char *body, size_t length,
                                 char *first, size_t first_capacity,
                                 char *second, size_t second_capacity) {
    size_t offset = 0U;
    return dbus_body_string_at(body, length, &offset, first, first_capacity) &&
           dbus_body_string_at(body, length, &offset, second, second_capacity) &&
           offset == length;
}

static int dbus_body_string_exact(const unsigned char *body, size_t length,
                                  char *value, size_t capacity) {
    if (!body || !value || capacity == 0U || length < 5U) return 0;
    uint32_t string_length = dbus_read_le32(body);
    if ((size_t)string_length + 5U != length ||
        (size_t)string_length >= capacity ||
        body[4U + string_length] != 0U)
        return 0;
    memcpy(value, body + 4U, string_length);
    value[string_length] = '\0';
    return 1;
}

static size_t dbus_append_body_string(unsigned char *body, size_t offset,
                                      size_t capacity, const char *value) {
    if (!body || !value) return 0U;
    size_t length = strlen(value);
    offset = dbus_align4(offset);
    if (offset > capacity || capacity - offset < 5U ||
        length >= capacity - offset - 4U) return 0U;
    dbus_write_le32(body + offset, (uint32_t)length);
    offset += 4U;
    memcpy(body + offset, value, length + 1U);
    return offset + length + 1U;
}

/* Append one D-Bus variant carrying a string.  The variant signature itself
 * has byte alignment; the contained string is then aligned to four bytes. */
static size_t dbus_append_variant_string(unsigned char *body, size_t offset,
                                         size_t capacity, const char *value) {
    if (!body || !value || offset + 3U > capacity) return 0U;
    body[offset++] = 1U;
    body[offset++] = 's';
    body[offset++] = 0U;
    return dbus_append_body_string(body, offset, capacity, value);
}

static size_t dbus_append_variant_bool(unsigned char *body, size_t offset,
                                       size_t capacity, int value) {
    if (!body || offset + 3U > capacity) return 0U;
    body[offset++] = 1U;
    body[offset++] = 'b';
    body[offset++] = 0U;
    offset = dbus_align4(offset);
    if (offset + 4U > capacity) return 0U;
    dbus_write_le32(body + offset, value ? 1U : 0U);
    return offset + 4U;
}

static const char *dbus_desktop_capabilities(void) {
    return "akaos.desktop.v1;session=dbus;display=x11,wayland;"
           "toolkit=gtk,qt;pthreads=shared";
}

static int dbus_append_desktop_property_variant_at(
    unsigned char *body, size_t offset, size_t capacity, const char *status,
    const char *property, size_t *length_out) {
    const char *string_value = NULL;
    int bool_value = 0;
    int is_bool = 0;
    if (!body || !status || !property || !length_out) return 0;
    if (strcmp(property, "Status") == 0) {
        string_value = status;
    } else if (strcmp(property, "Capabilities") == 0) {
        string_value = dbus_desktop_capabilities();
    } else if (strcmp(property, "DisplayServer") == 0) {
        string_value = "x11,wayland";
    } else if (strcmp(property, "Toolkit") == 0) {
        string_value = "gtk,qt";
    } else if (strcmp(property, "SessionType") == 0) {
        string_value = "akaos-native";
    } else if (strcmp(property, "Ready") == 0) {
        bool_value = 1;
        is_bool = 1;
    } else {
        return 0;
    }
    size_t end = is_bool
        ? dbus_append_variant_bool(body, offset, capacity, bool_value)
        : dbus_append_variant_string(body, offset, capacity, string_value);
    if (end == 0U || end < offset) return 0;
    *length_out = end - offset;
    return 1;
}

static int dbus_append_desktop_property_variant(
    unsigned char *body, size_t capacity, const char *status,
    const char *property, size_t *length_out) {
    return dbus_append_desktop_property_variant_at(
        body, 0U, capacity, status, property, length_out);
}

static size_t dbus_append_desktop_property_entry(
    unsigned char *body, size_t offset, size_t capacity, const char *status,
    const char *property) {
    offset = dbus_align8(offset);
    size_t next = dbus_append_body_string(body, offset, capacity, property);
    if (next == 0U) return 0U;
    size_t variant_length = 0U;
    if (!dbus_append_desktop_property_variant_at(
            body, next, capacity, status, property, &variant_length))
        return 0U;
    return next + variant_length;
}

/* Keep property names in a switch rather than a static pointer table.  akaOS
 * user images are relocated as a whole and deliberately avoid pointer-table
 * relocations in resident service code. */
static const char *AKAOS_DBUS_NOOPT dbus_desktop_property_name(size_t index) {
    switch (index) {
    case 0U: return "Status";
    case 1U: return "Ready";
    case 2U: return "Capabilities";
    case 3U: return "DisplayServer";
    case 4U: return "Toolkit";
    case 5U: return "SessionType";
    default: return NULL;
    }
}

static int dbus_append_desktop_properties(unsigned char *body, size_t capacity,
                                          const char *status,
                                          size_t *length_out) {
    if (!body || !status || !length_out || capacity < 8U) return 0;
    memset(body, 0, capacity);
    size_t offset = 8U; /* four-byte array length plus element alignment */
    for (size_t index = 0U; index < 6U; ++index) {
        const char *property = dbus_desktop_property_name(index);
        offset = dbus_append_desktop_property_entry(
            body, offset, capacity, status, property);
        if (offset == 0U) return 0;
    }
    dbus_write_le32(body, (uint32_t)(offset - 8U));
    *length_out = offset;
    return 1;
}

/* Encode the bounded PropertiesChanged payload used by native desktop roles.
 * The array element start is aligned against the complete message body, not
 * a temporary sub-buffer; this matters when the first interface string is not
 * eight-byte aligned.  Every published desktop property is included so a
 * standard client can update its complete cached state from one signal.  The
 * invalidated-property array remains empty because these values are all
 * explicitly supplied. */
static int dbus_append_desktop_properties_changed_body(
    unsigned char *body, size_t capacity, const char *status,
    size_t *length_out) {
    if (!body || !status || !length_out || capacity < 16U) return 0;
    memset(body, 0, capacity);
    size_t offset = dbus_append_body_string(
        body, 0U, capacity, "org.akaos.Desktop");
    if (offset == 0U) return 0;
    size_t array_offset = dbus_align4(offset);
    if (array_offset > capacity || capacity - array_offset < 4U) return 0;
    size_t entries = dbus_align8(array_offset + 4U);
    if (entries > capacity) return 0;
    size_t end = entries;
    for (size_t index = 0U; index < 6U; ++index) {
        const char *property = dbus_desktop_property_name(index);
        end = dbus_append_desktop_property_entry(
            body, end, capacity, status, property);
        if (end == 0U || end < entries || end - entries > UINT32_MAX)
            return 0;
    }
    dbus_write_le32(body + array_offset, (uint32_t)(end - entries));
    offset = end;
    size_t invalidated = dbus_align4(offset);
    if (invalidated > capacity || capacity - invalidated < 4U) return 0;
    dbus_write_le32(body + invalidated, 0U);
    *length_out = invalidated + 4U;
    return 1;
}

static int dbus_send_signal_frame(int descriptor, const char *path,
                                  const char *interface_name,
                                  const char *member, const char *signature,
                                  const unsigned char *body,
                                  size_t body_length) {
    if (descriptor < 0 || !path || path[0] != '/' || !interface_name ||
        interface_name[0] == '\0' || !member || member[0] == '\0' ||
        (body_length != 0U && !body)) {
        errno = EINVAL;
        return 0;
    }
    if (akaos_dbus_object_path_validate(path) != 0 ||
        (signature && signature[0] != '\0' &&
         (akaos_dbus_signature_validate(signature, NULL) != 0 ||
          akaos_dbus_body_validate(signature, body, body_length, NULL) != 0)) ||
        ((!signature || signature[0] == '\0') && body_length != 0U)) {
        errno = EINVAL;
        return 0;
    }
    unsigned char frame[2048];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 4U; /* SIGNAL */
    frame[3] = 1U; /* protocol version */
    unsigned char *fields = frame + 16U;
    size_t fields_length = dbus_append_string_field(
        fields, 0U, 1U, 'o', path);
    fields_length = dbus_append_string_field(
        fields, fields_length, 2U, 's', interface_name);
    fields_length = dbus_append_string_field(
        fields, fields_length, 3U, 's', member);
    if (signature && signature[0] != '\0')
        fields_length = dbus_append_signature_field(
            fields, fields_length, signature);
    fields_length = dbus_align8(fields_length);
    if (fields_length > sizeof(frame) - 16U ||
        body_length > sizeof(frame) - 16U - fields_length) {
        errno = EMSGSIZE;
        return 0;
    }
    if (body_length != 0U)
        memcpy(frame + 16U + fields_length, body, body_length);
    dbus_write_le32(frame + 4U, (uint32_t)body_length);
    dbus_write_le32(frame + 8U, 1U);
    dbus_write_le32(frame + 12U, (uint32_t)fields_length);
    return write_all_bytes(descriptor, frame,
                           16U + fields_length + body_length);
}

int akaos_dbus_emit_desktop_properties_changed(int descriptor,
                                               const char *status) {
    if (descriptor < 0 || !status || status[0] == '\0' ||
        strlen(status) >= 128U) {
        errno = EINVAL;
        return -1;
    }
    unsigned char body[512];
    size_t body_length = 0U;
    if (!dbus_append_desktop_properties_changed_body(
            body, sizeof(body), status, &body_length) ||
        !dbus_send_signal_frame(
            descriptor, "/org/akaos/Desktop",
            "org.freedesktop.DBus.Properties", "PropertiesChanged",
            "sa{sv}as", body, body_length)) {
        if (errno == 0) errno = EIO;
        return -1;
    }
    return 0;
}

static int dbus_body_variant_string_nonempty(const unsigned char *body,
                                             size_t length) {
    if (!body || length < 9U || body[0] != 1U || body[1] != 's' ||
        body[2] != 0U)
        return 0;
    size_t offset = dbus_align4(3U);
    if (offset + 5U > length) return 0;
    uint32_t string_length = dbus_read_le32(body + offset);
    offset += 4U;
    return string_length > 0U && (size_t)string_length + 1U == length - offset &&
           body[offset + string_length] == 0U;
}

static int dbus_body_properties_have(const unsigned char *body, size_t length,
                                     const char *wanted_first,
                                     const char *wanted_second,
                                     const char *wanted_third) {
    if (!body || length < 8U || !wanted_first || !wanted_second ||
        !wanted_third)
        return 0;
    uint32_t array_length = dbus_read_le32(body);
    if ((size_t)array_length > length - 8U) return 0;
    size_t offset = 8U;
    size_t end = offset + (size_t)array_length;
    int first = 0;
    int second = 0;
    int third = 0;
    while (offset < end) {
        offset = dbus_align8(offset);
        if (offset >= end) return 0;
        char key[64];
        if (!dbus_body_string_at(body, end, &offset, key, sizeof(key)))
            return 0;
        if (strcmp(key, wanted_first) == 0) first = 1;
        if (strcmp(key, wanted_second) == 0) second = 1;
        if (strcmp(key, wanted_third) == 0) third = 1;
        if (offset + 3U > end) return 0;
        unsigned char signature_length = body[offset++];
        if (signature_length != 1U || offset + 2U > end) return 0;
        char value_signature = (char)body[offset++];
        if (body[offset++] != 0U) return 0;
        if (value_signature == 's') {
            char value[160];
            if (!dbus_body_string_at(body, end, &offset, value,
                                     sizeof(value)))
                return 0;
        } else if (value_signature == 'b') {
            offset = dbus_align4(offset);
            if (offset + 4U > end) return 0;
            offset += 4U;
        } else {
            return 0;
        }
    }
    return offset == end && first && second && third;
}

int akaos_dbus_service_dispatch(int descriptor, const char *status,
                                int timeout_ms) {
    if (descriptor < 0 || !status || timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd descriptor_state;
    descriptor_state.fd = descriptor;
    descriptor_state.events = POLLIN;
    descriptor_state.revents = 0;
    int ready = poll(&descriptor_state, 1U, timeout_ms);
    if (ready == 0) return 0;
    if (ready < 0) return -1;
    if ((descriptor_state.revents & (POLLIN | POLLHUP | POLLERR)) == 0)
        return 0;
    unsigned char frame[1024];
    size_t frame_length = 0;
    if (!dbus_read_frame(descriptor, frame, sizeof(frame), &frame_length) ||
        frame_length < 16U || frame[1] != 1U) {
        errno = EIO;
        return -1;
    }
    const char *reply = status;
    char member[64];
    char interface_name[96] = {0};
    char path[96] = {0};
    char signature[64] = {0};
    size_t fields_length = (size_t)dbus_read_le32(frame + 12U);
    if (!dbus_find_header_string(frame + 16U, fields_length, 3U,
                                 member, sizeof(member))) {
        errno = EIO;
        return -1;
    }
    (void)dbus_find_header_string(frame + 16U, fields_length, 1U,
                                  path, sizeof(path));
    (void)dbus_find_header_string(frame + 16U, fields_length, 2U,
                                  interface_name, sizeof(interface_name));
    (void)dbus_find_header_signature(frame + 16U, fields_length,
                                     signature, sizeof(signature));
    size_t body_offset = 16U + dbus_align8(fields_length);
    size_t body_length = (size_t)dbus_read_le32(frame + 4U);
    if (body_offset > frame_length || body_length > frame_length - body_offset) {
        errno = EIO;
        return -1;
    }
    const unsigned char *request_body = frame + body_offset;
    if (akaos_dbus_object_path_validate(path) != 0 ||
        (signature[0] != '\0' &&
         akaos_dbus_body_validate(signature, request_body, body_length, NULL) !=
             0) ||
        (signature[0] == '\0' && body_length != 0U)) {
        if (!dbus_send_error_frame(
                descriptor, dbus_read_le32(frame + 8U),
                "org.freedesktop.DBus.Error.InvalidArgs",
                "Invalid object path, signature, or body")) {
            errno = EIO;
            return -1;
        }
        return 1;
    }

    /* Standard desktop toolkits use this interface before they construct a
     * window.  Keep the property surface small and deterministic, but use
     * real D-Bus variants and a{sv} encoding instead of a string-only shim. */
    if (strcmp(interface_name, "org.freedesktop.DBus.Properties") == 0) {
        if (strcmp(member, "Get") == 0 && strcmp(signature, "ss") == 0) {
            char requested_interface[96];
            char property[64];
            if (!dbus_body_two_strings(request_body, body_length,
                                       requested_interface,
                                       sizeof(requested_interface), property,
                                       sizeof(property))) {
                if (!dbus_send_error_frame(
                        descriptor, dbus_read_le32(frame + 8U),
                        "org.freedesktop.DBus.Error.InvalidArgs",
                        "Properties.Get requires an interface and property")) {
                    errno = EIO;
                    return -1;
                }
                return 1;
            }
            if (strcmp(requested_interface, "org.akaos.Desktop") != 0) {
                if (!dbus_send_error_frame(
                        descriptor, dbus_read_le32(frame + 8U),
                        "org.freedesktop.DBus.Error.UnknownInterface",
                        "The akaOS desktop property interface is unknown")) {
                    errno = EIO;
                    return -1;
                }
                return 1;
            }
            unsigned char property_body[256];
            size_t property_length = 0U;
            if (!dbus_append_desktop_property_variant(
                    property_body, sizeof(property_body), status, property,
                    &property_length)) {
                if (!dbus_send_error_frame(
                        descriptor, dbus_read_le32(frame + 8U),
                        "org.freedesktop.DBus.Error.UnknownProperty",
                        "The requested akaOS desktop property is unknown")) {
                    errno = EIO;
                    return -1;
                }
                return 1;
            }
            if (!dbus_send_method_return_frame(
                    descriptor, dbus_read_le32(frame + 8U), "v",
                    property_body, property_length)) {
                errno = EIO;
                return -1;
            }
            return 1;
        }
        if (strcmp(member, "GetAll") == 0 && strcmp(signature, "s") == 0) {
            char requested_interface[96];
            if (!dbus_body_string_exact(request_body, body_length,
                                        requested_interface,
                                        sizeof(requested_interface))) {
                if (!dbus_send_error_frame(
                        descriptor, dbus_read_le32(frame + 8U),
                        "org.freedesktop.DBus.Error.InvalidArgs",
                        "Properties.GetAll requires an interface")) {
                    errno = EIO;
                    return -1;
                }
                return 1;
            }
            if (strcmp(requested_interface, "org.akaos.Desktop") != 0) {
                if (!dbus_send_error_frame(
                        descriptor, dbus_read_le32(frame + 8U),
                        "org.freedesktop.DBus.Error.UnknownInterface",
                        "The akaOS desktop property interface is unknown")) {
                    errno = EIO;
                    return -1;
                }
                return 1;
            }
            unsigned char property_body[1024];
            size_t property_length = 0U;
            if (!dbus_append_desktop_properties(
                    property_body, sizeof(property_body), status,
                    &property_length) ||
                !dbus_send_method_return_frame(
                    descriptor, dbus_read_le32(frame + 8U), "a{sv}",
                    property_body, property_length)) {
                errno = EIO;
                return -1;
            }
            return 1;
        }
        if (strcmp(member, "Set") == 0) {
            if (!dbus_send_error_frame(
                    descriptor, dbus_read_le32(frame + 8U),
                    "org.freedesktop.DBus.Error.PropertyReadOnly",
                    "akaOS desktop properties are read-only")) {
                errno = EIO;
                return -1;
            }
            return 1;
        }
        if (!dbus_send_error_frame(
                descriptor, dbus_read_le32(frame + 8U),
                "org.freedesktop.DBus.Error.UnknownMethod",
                "The akaOS desktop properties service does not implement this method")) {
            errno = EIO;
            return -1;
        }
        return 1;
    }

    int known_method = 1;
    if (strcmp(member, "GetCapabilities") == 0) {
        reply = dbus_desktop_capabilities();
    } else if (strcmp(member, "Ping") == 0) {
        reply = "pong";
    } else if (strcmp(member, "Introspect") == 0) {
        reply = "<node><interface name=\"org.freedesktop.DBus.Introspectable\">"
                "<method name=\"Introspect\"><arg direction=\"out\" "
                "type=\"s\"/></method></interface><interface "
                "name=\"org.akaos.Desktop\"><method name=\"GetStatus\">"
                "<arg direction=\"out\" type=\"s\"/></method><method "
                "name=\"GetCapabilities\"><arg direction=\"out\" "
                "type=\"s\"/></method></interface><interface "
                "name=\"org.freedesktop.DBus.Properties\"><method "
                "name=\"Get\"><arg direction=\"in\" type=\"s\"/>"
                "<arg direction=\"in\" type=\"s\"/><arg "
                "direction=\"out\" type=\"v\"/></method><method "
                "name=\"GetAll\"><arg direction=\"in\" type=\"s\"/>"
                "<arg direction=\"out\" type=\"a{sv}\"/></method><method "
                "name=\"Set\"><arg direction=\"in\" type=\"s\"/>"
                "<arg direction=\"in\" type=\"s\"/><arg "
                "direction=\"in\" type=\"v\"/></method></interface></node>";
    } else if (strcmp(member, "GetStatus") != 0) {
        known_method = 0;
    }
    if (!known_method) {
        if (!dbus_send_error_frame(
                descriptor, dbus_read_le32(frame + 8U),
                "org.freedesktop.DBus.Error.UnknownMethod",
                "The akaOS desktop service does not implement this method")) {
            errno = EIO;
            return -1;
        }
        return 1;
    }
    size_t reply_length = strlen(reply);
    if (reply_length >= 128U && strcmp(member, "Introspect") != 0)
        reply_length = 127U;
    unsigned char body[1024];
    if (reply_length >= sizeof(body) - 5U) {
        errno = EOVERFLOW;
        return -1;
    }
    dbus_write_le32(body, (uint32_t)reply_length);
    memcpy(body + 4U, reply, reply_length);
    body[4U + reply_length] = '\0';
    if (!dbus_send_method_return_frame(
            descriptor, dbus_read_le32(frame + 8U), "s", body,
            reply_length + 5U)) {
        errno = EIO;
        return -1;
    }
    return 1;
}

int akaos_dbus_binary_desktop_service_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) {
        printf("D-Bus desktop service bus-open failed (errno=%d)\n", errno);
        return -1;
    }
    unsigned char body[1024];
    size_t body_length = 0;
    const char *failure_stage = "Hello/GetStatus";
    int result = dbus_binary_call(
                     descriptor, 1U, "Hello", NULL, NULL, 0U,
                     body, sizeof(body), &body_length) &&
                 dbus_binary_call_to(
                     descriptor, 2U, "org.akaos.LXDE.Openbox", "GetStatus",
                     NULL, NULL, 0U, body, sizeof(body), &body_length);
    if (result && body_length >= 5U) {
        uint32_t length = dbus_read_le32(body);
        result = length > 0U && (size_t)length + 5U == body_length &&
                 body[4U + length] == '\0';
    } else {
        result = 0;
    }
    if (result) {
        failure_stage = "GetCapabilities";
        result = dbus_binary_call_to(
                     descriptor, 3U, "org.akaos.LXDE.Openbox",
                     "GetCapabilities", NULL, NULL, 0U, body, sizeof(body),
                     &body_length) &&
                 dbus_body_string_contains(body, body_length,
                                           "display=x11,wayland") &&
                 dbus_body_string_contains(body, body_length,
                                           "toolkit=gtk,qt");
    }
    if (result) {
        failure_stage = "Introspect";
        result = dbus_binary_call_to(
                     descriptor, 4U, "org.akaos.LXDE.Openbox", "Introspect",
                     NULL, NULL, 0U, body, sizeof(body), &body_length) &&
                 dbus_body_string_contains(body, body_length,
                                           "org.akaos.Desktop") &&
                 dbus_body_string_contains(body, body_length, "GetStatus") &&
                 dbus_body_string_contains(body, body_length,
                                           "org.freedesktop.DBus.Properties");
    }
    if (result) {
        failure_stage = "Properties.Get";
        unsigned char request[256];
        size_t request_length = dbus_append_body_string(
            request, 0U, sizeof(request), "org.akaos.Desktop");
        request_length = dbus_append_body_string(
            request, request_length, sizeof(request), "Status");
        result = request_length != 0U &&
                 dbus_binary_call_to_endpoint(
                     descriptor, 5U, "org.akaos.LXDE.Openbox",
                     "/org/akaos/Desktop",
                     "org.freedesktop.DBus.Properties", "Get", "ss",
                     request, request_length, body, sizeof(body),
                     &body_length) &&
                 dbus_body_variant_string_nonempty(body, body_length);
    }
    if (result) {
        failure_stage = "Properties.GetAll";
        unsigned char request[128];
        size_t request_length = dbus_append_body_string(
            request, 0U, sizeof(request), "org.akaos.Desktop");
        result = request_length != 0U &&
                 dbus_binary_call_to_endpoint(
                     descriptor, 6U, "org.akaos.LXDE.Openbox",
                     "/org/akaos/Desktop",
                     "org.freedesktop.DBus.Properties", "GetAll", "s",
                     request, request_length, body, sizeof(body),
                     &body_length) &&
                 dbus_body_properties_have(body, body_length, "Status", "Ready",
                                           "Capabilities");
    }
    close(descriptor);
    if (!result) {
        printf("D-Bus desktop property probe failed at %s\n", failure_stage);
        if (strcmp(failure_stage, "Properties.GetAll") == 0) {
            printf("GetAll reply length=%u head=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                   (unsigned)body_length, body[0], body[1], body[2], body[3],
                   body[4], body[5], body[6], body[7]);
        }
        errno = EIO;
    }
    return result ? 0 : -1;
}

static int dbus_body_properties_changed_status(const unsigned char *body,
                                               size_t length,
                                               const char *expected_status) {
    if (!body || !expected_status || length < 16U) return 0;
    size_t offset = 0U;
    char interface_name[96];
    if (!dbus_body_string_at(body, length, &offset, interface_name,
                             sizeof(interface_name)) ||
        strcmp(interface_name, "org.akaos.Desktop") != 0)
        return 0;
    size_t array_offset = dbus_align4(offset);
    if (array_offset > length || length - array_offset < 4U) return 0;
    uint32_t array_length = dbus_read_le32(body + array_offset);
    size_t entries = dbus_align8(array_offset + 4U);
    if (entries > length || (size_t)array_length > length - entries) return 0;
    size_t end = entries + (size_t)array_length;
    if (entries == end) return 0;

    /* Require every published property exactly once.  The signal is allowed
     * to use any dictionary order, as required by the D-Bus a{sv} contract. */
    uint32_t seen = 0U;
    offset = entries;
    while (offset < end) {
        offset = dbus_align8(offset);
        if (offset >= end) return 0;
        char property[64];
        if (!dbus_body_string_at(body, end, &offset, property,
                                 sizeof(property)) || offset + 3U > end)
            return 0;
        unsigned char signature_length = body[offset++];
        if (signature_length != 1U || offset + 2U > end ||
            body[offset + 1U] != 0U)
            return 0;
        char value_signature = (char)body[offset++];
        ++offset; /* variant-signature NUL */
        uint32_t bit = 0U;
        int expected_bool = 0;
        const char *expected_string = NULL;
        if (strcmp(property, "Status") == 0) {
            bit = UINT32_C(1) << 0;
            expected_string = expected_status;
        } else if (strcmp(property, "Ready") == 0) {
            bit = UINT32_C(1) << 1;
            expected_bool = 1;
        } else if (strcmp(property, "Capabilities") == 0) {
            bit = UINT32_C(1) << 2;
            expected_string = dbus_desktop_capabilities();
        } else if (strcmp(property, "DisplayServer") == 0) {
            bit = UINT32_C(1) << 3;
            expected_string = "x11,wayland";
        } else if (strcmp(property, "Toolkit") == 0) {
            bit = UINT32_C(1) << 4;
            expected_string = "gtk,qt";
        } else if (strcmp(property, "SessionType") == 0) {
            bit = UINT32_C(1) << 5;
            expected_string = "akaos-native";
        } else {
            return 0;
        }
        if ((seen & bit) != 0U) return 0;
        if ((expected_string && value_signature != 's') ||
            (!expected_string && value_signature != 'b'))
            return 0;
        if (expected_string) {
            char value[256];
            if (!dbus_body_string_at(body, end, &offset, value,
                                     sizeof(value)) ||
                strcmp(value, expected_string) != 0)
                return 0;
        } else {
            offset = dbus_align4(offset);
            if (offset + 4U > end ||
                dbus_read_le32(body + offset) != (expected_bool ? 1U : 0U))
                return 0;
            offset += 4U;
        }
        seen |= bit;
    }
    if (seen != UINT32_C(0x3f) || offset != end) return 0;

    /* The invalidated-property array is intentionally empty. */
    size_t invalidated = dbus_align4(end);
    if (invalidated > length || length - invalidated != 4U ||
        dbus_read_le32(body + invalidated) != 0U)
        return 0;
    return 1;
}

int akaos_dbus_binary_properties_signal_test(void) {
    int observer = dbus_binary_open();
    int sender = observer >= 0 ? dbus_binary_open() : -1;
    unsigned char body[1024];
    size_t length = 0U;
    const char rule[] =
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged'";
    unsigned char rule_body[192];
    size_t rule_length = strlen(rule);
    dbus_write_le32(rule_body, (uint32_t)rule_length);
    memcpy(rule_body + 4U, rule, rule_length + 1U);
    int result = observer >= 0 && sender >= 0 &&
                 dbus_binary_call(observer, 1U, "Hello", NULL, NULL, 0U,
                                  body, sizeof(body), &length) &&
                 dbus_binary_call(observer, 2U, "AddMatch", "s", rule_body,
                                  rule_length + 5U, body, sizeof(body),
                                  &length) &&
                 dbus_binary_call(sender, 1U, "Hello", NULL, NULL, 0U,
                                  body, sizeof(body), &length) &&
                 akaos_dbus_emit_desktop_properties_changed(sender, "ready") ==
                     0;
    unsigned char frame[2048];
    size_t frame_length = 0U;
    char path[96];
    char interface_name[96];
    char member[96];
    char signature[64];
    if (result)
        result = dbus_read_frame(observer, frame, sizeof(frame), &frame_length) &&
                 frame[1] == 4U &&
                 dbus_find_header_string(
                     frame + 16U, dbus_read_le32(frame + 12U), 1U,
                     path, sizeof(path)) &&
                 dbus_find_header_string(
                     frame + 16U, dbus_read_le32(frame + 12U), 2U,
                     interface_name, sizeof(interface_name)) &&
                 dbus_find_header_string(
                     frame + 16U, dbus_read_le32(frame + 12U), 3U,
                     member, sizeof(member)) &&
                 dbus_find_header_signature(
                     frame + 16U, dbus_read_le32(frame + 12U), signature,
                     sizeof(signature)) &&
                 strcmp(path, "/org/akaos/Desktop") == 0 &&
                 strcmp(interface_name, "org.freedesktop.DBus.Properties") ==
                     0 &&
                 strcmp(member, "PropertiesChanged") == 0 &&
                 strcmp(signature, "sa{sv}as") == 0 &&
                 dbus_body_properties_changed_status(
                     frame + 16U + dbus_align8(dbus_read_le32(frame + 12U)),
                     dbus_read_le32(frame + 4U), "ready");
    if (sender >= 0) close(sender);
    if (observer >= 0) close(observer);
    if (!result) errno = EIO;
    return result ? 0 : -1;
}

struct dbus_route_service_context {
    volatile int ready;
    volatile int failed;
    volatile unsigned stage;
};

static void *dbus_route_service_worker(void *argument) {
    struct dbus_route_service_context *context = argument;
    if (!context) return 0;
    int descriptor = dbus_binary_open();
    if (descriptor < 0) {
        context->failed = 1;
        return 0;
    }
    context->stage = 1U;
    unsigned char body[1024];
    size_t length = 0;
    static const char name[] = "com.akaos.Echo";
    unsigned char name_body[64];
    size_t name_length = strlen(name);
    dbus_write_le32(name_body, (uint32_t)name_length);
    memcpy(name_body + 4, name, name_length + 1U);
    if (!dbus_binary_call(descriptor, 1, "Hello", NULL, NULL, 0, body,
                          sizeof(body), &length) ||
        !dbus_binary_call(descriptor, 2, "RequestName", "s", name_body,
                          name_length + 5U, body, sizeof(body), &length) ||
        !dbus_body_uint_equals(body, length, 1U)) {
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 2U;
    context->ready = 1;
    unsigned char frame[1024];
    size_t frame_length = 0;
    struct pollfd request_watch = {descriptor, POLLIN, 0};
    int request_ready;
    do {
        request_ready = poll(&request_watch, 1U, 5000);
    } while (request_ready < 0 && errno == EINTR);
    if (request_ready <= 0 ||
        (request_watch.revents & (POLLIN | POLLHUP)) == 0) {
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    if (!dbus_read_frame(descriptor, frame, sizeof(frame), &frame_length)) {
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 3U;
    if (frame[1] != 1) {
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    if (!dbus_send_method_return_frame(
            descriptor, dbus_read_le32(frame + 8), "s",
            frame + 16 + dbus_align8((size_t)dbus_read_le32(frame + 12)),
            (size_t)dbus_read_le32(frame + 4))) {
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 4U;
    int request_fds[AKAOS_DBUS_MAX_UNIX_FDS] = {-1, -1, -1, -1};
    size_t request_fd_count = 0U;
    int request_received = dbus_read_frame_with_fds(
        descriptor, frame, sizeof(frame), &frame_length, request_fds,
        &request_fd_count);
    if (!request_received) {
        fprintf(stderr,
                "D-Bus descriptor-route receiver read failed (errno=%d)\n",
                errno);
        dbus_close_fd_array(request_fds, &request_fd_count);
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    if (frame[1] != 1U || request_fd_count != 2U ||
        dbus_read_le32(frame + 4U) != 8U ||
        dbus_read_le32(frame + 16U +
                       dbus_align8(dbus_read_le32(frame + 12U))) != 0U ||
        dbus_read_le32(frame + 20U +
                       dbus_align8(dbus_read_le32(frame + 12U))) != 1U) {
        fprintf(stderr,
                "D-Bus descriptor-route receiver got type=%u fds=%u "
                "body=%u handle=%u\n",
                (unsigned)frame[1], (unsigned)request_fd_count,
                (unsigned)dbus_read_le32(frame + 4U),
                (unsigned)dbus_read_le32(
                    frame + 16U + dbus_align8(dbus_read_le32(frame + 12U))));
        dbus_close_fd_array(request_fds, &request_fd_count);
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 5U;
    size_t signature_length =
        (size_t)dbus_read_le32(frame + 12U);
    char signature[16];
    if (!dbus_find_header_signature(frame + 16U, signature_length, signature,
                                    sizeof(signature)) ||
        strcmp(signature, "hh") != 0) {
        dbus_close_fd_array(request_fds, &request_fd_count);
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 6U;
    const unsigned char service_markers[2] = {'S', 'T'};
    if (write(request_fds[0], &service_markers[0], 1U) != 1 ||
        write(request_fds[1], &service_markers[1], 1U) != 1 ||
        !dbus_send_method_return_frame_with_fds(
            descriptor, dbus_read_le32(frame + 8U), "hh",
            frame + 16U + dbus_align8(dbus_read_le32(frame + 12U)), 8U,
            request_fds, request_fd_count)) {
        dbus_close_fd_array(request_fds, &request_fd_count);
        close(descriptor);
        context->failed = 1;
        return 0;
    }
    context->stage = 7U;
    dbus_close_fd_array(request_fds, &request_fd_count);
    context->stage = 8U;
    close(descriptor);
    return 0;
}

int akaos_dbus_binary_route_test(void) {
    struct dbus_route_service_context service = {0, 0, 0U};
    pthread_t service_thread;
    if (pthread_create(&service_thread, NULL, dbus_route_service_worker,
                       &service) != 0) return -1;
    for (unsigned count = 0; count < 2000U && !service.ready && !service.failed;
         ++count) (void)sched_yield();
    if (!service.ready || service.failed) {
        (void)pthread_join(service_thread, NULL);
        errno = EIO;
        return -1;
    }
    int caller = dbus_binary_open();
    unsigned char body[1024];
    size_t length = 0;
    static const char request[] = "route-ok";
    unsigned char request_body[32];
    size_t request_length = sizeof(request) - 1U;
    dbus_write_le32(request_body, (uint32_t)request_length);
    memcpy(request_body + 4, request, request_length + 1U);
    int result = caller >= 0 &&
                 dbus_binary_call(caller, 1, "Hello", NULL, NULL, 0, body,
                                  sizeof(body), &length) &&
                 dbus_binary_call_to(caller, 2, "com.akaos.Echo", "Echo", "s",
                                     request_body, request_length + 5U, body,
                                     sizeof(body), &length) &&
                 length == request_length + 5U &&
                 dbus_read_le32(body) == request_length &&
                 memcmp(body + 4, request, request_length + 1U) == 0;
    int pipe_fds[2][2] = {{-1, -1}, {-1, -1}};
    struct akaos_dbus_async_client async_client;
    int async_initialized = 0;
    unsigned client_stage = 0U;
    int returned_fds[AKAOS_DBUS_MAX_UNIX_FDS] = {-1, -1, -1, -1};
    size_t returned_fd_count = 0U;
    int negative_cases_ok = 1;
    int route_ok = 0;
    if (result && pipe(pipe_fds[0]) == 0 && pipe(pipe_fds[1]) == 0 &&
        akaos_dbus_async_client_init(&async_client, caller) == 0) {
        async_initialized = 1;
        client_stage = 1U;
        unsigned char handle_body[8] = {0, 0, 0, 0, 1, 0, 0, 0};
        int sent_fds[2] = {pipe_fds[0][1], pipe_fds[1][1]};
        uint32_t serial = 0U;
        int queued = akaos_dbus_async_client_queue_call_with_fds(
            &async_client, "com.akaos.Echo", "/com/akaos/Echo",
            "com.akaos.Echo", "PassFD", "hh", handle_body,
            sizeof(handle_body), sent_fds, 2U, &serial);
        if (queued != 0) {
            fprintf(stderr, "D-Bus descriptor-route queue failed (errno=%d)\n",
                    errno);
            result = 0;
        } else {
            client_stage = 2U;
        }
        if (queued == 0) {
            (void)close(pipe_fds[0][1]);
            pipe_fds[0][1] = -1;
            (void)close(pipe_fds[1][1]);
            pipe_fds[1][1] = -1;
        }
        int reply_ready = 0;
        for (unsigned attempt = 0U; queued == 0 && attempt < 2000U;
             ++attempt) {
            struct pollfd watch = {caller, POLLIN, 0};
            int ready = poll(&watch, 1U, 5);
            if (ready < 0 && errno != EINTR) {
                client_stage = 3U;
                break;
            }
            if (ready > 0 &&
                akaos_dbus_async_client_pump(&async_client, watch.revents) < 0) {
                client_stage = 4U;
                break;
            }
            size_t reply_length = 0U;
            int take = akaos_dbus_async_client_take_reply_with_fds(
                &async_client, serial, body, sizeof(body), &reply_length,
                returned_fds, 1U,
                &returned_fd_count, NULL);
            if (take < 0 && errno == EOVERFLOW) {
                reply_ready = 1;
                break;
            }
            if (take < 0 || take > 0) {
                client_stage = 5U;
                break;
            }
        }
        route_ok = queued == 0 && reply_ready;
        if (route_ok) {
            int take = akaos_dbus_async_client_take_reply_with_fds(
                &async_client, serial, body, sizeof(body), &length,
                returned_fds, 2U, &returned_fd_count, NULL);
            route_ok = take == 1 && length == sizeof(handle_body) &&
                       dbus_read_le32(body) == 0U &&
                       dbus_read_le32(body + 4U) == 1U &&
                       returned_fd_count == 2U;
            if (route_ok) client_stage = 6U;
        }
        if (route_ok) {
            unsigned char response_markers[2] = {0U, 0U};
            const unsigned char service_markers[2] = {'S', 'T'};
            const unsigned char client_markers[2] = {'C', 'D'};
            for (size_t index = 0U; route_ok && index < 2U; ++index)
                route_ok = read_exact_bytes(pipe_fds[index][0],
                                            &response_markers[index], 1U) &&
                           response_markers[index] == service_markers[index] &&
                           write(returned_fds[index], &client_markers[index],
                                 1U) == 1;
            for (size_t index = 0U; route_ok && index < 2U; ++index)
                route_ok = read_exact_bytes(pipe_fds[index][0],
                                            &response_markers[index], 1U) &&
                           response_markers[index] == client_markers[index];
            if (route_ok) client_stage = 7U;
        }
    } else {
        result = 0;
    }
    dbus_close_fd_array(returned_fds, &returned_fd_count);
    for (size_t pipe_index = 0U; pipe_index < 2U; ++pipe_index) {
        if (pipe_fds[pipe_index][0] >= 0) close(pipe_fds[pipe_index][0]);
        if (pipe_fds[pipe_index][1] >= 0) close(pipe_fds[pipe_index][1]);
    }
    (void)pthread_join(service_thread, NULL);
    if (route_ok && async_initialized) {
        unsigned char handle_body[8] = {0, 0, 0, 0, 1, 0, 0, 0};
        unsigned char invalid_handle_body[sizeof(handle_body)];
        memcpy(invalid_handle_body, handle_body, sizeof(handle_body));
        dbus_write_le32(invalid_handle_body + 4U, 2U);
        int validation_fds[2] = {0, 0};
        int invalid_index_result =
            akaos_dbus_async_client_queue_call_with_fds(
                &async_client, "com.akaos.Echo", "/com/akaos/Echo",
                "com.akaos.Echo", "PassFD", "hh", invalid_handle_body,
                sizeof(invalid_handle_body), validation_fds, 2U, NULL);
        if (invalid_index_result != -1 || errno != EBADF)
            negative_cases_ok = 0;
        int excessive_count_result =
            akaos_dbus_async_client_queue_call_with_fds(
                &async_client, "com.akaos.Echo", "/com/akaos/Echo",
                "com.akaos.Echo", "PassFD", "hh", handle_body,
                sizeof(handle_body), validation_fds,
                AKAOS_DBUS_MAX_UNIX_FDS + 1U, NULL);
        if (excessive_count_result != -1 || errno != EINVAL)
            negative_cases_ok = 0;
    }
    result = result && route_ok && negative_cases_ok;
    if (async_initialized)
        (void)akaos_dbus_async_client_close(&async_client);
    else if (caller >= 0)
        close(caller);
    if (!result)
        fprintf(stderr,
                "D-Bus descriptor-route client failed at stage %u (errno=%d)\n",
                client_stage, errno);
    if (service.failed || service.stage != 8U)
        fprintf(stderr, "D-Bus descriptor-route service failed at stage %u\n",
                service.stage);
    if (!result && errno == 0) errno = EIO;
    return result ? 0 : -1;
}

/* A small real service used by the activation regression.  It registers a
 * well-known name, consumes one routed Echo call, and replies with the same
 * string body before exiting.  The broker starts this binary on demand; no
 * test-side prelaunch is involved. */
static int dbus_binary_echo_service(const char *name) {
    if (!name || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char body[1024];
    size_t length = 0;
    unsigned char name_body[128];
    size_t name_length = strlen(name);
    if (name_length >= sizeof(name_body) - 5U) {
        close(descriptor);
        errno = EINVAL;
        return -1;
    }
    dbus_write_le32(name_body, (uint32_t)name_length);
    memcpy(name_body + 4, name, name_length + 1U);
    if (!dbus_binary_call(descriptor, 1, "Hello", NULL, NULL, 0, body,
                          sizeof(body), &length) ||
        !dbus_binary_call(descriptor, 2, "RequestName", "s", name_body,
                          name_length + 5U, body, sizeof(body), &length) ||
        !dbus_body_uint_equals(body, length, 1U)) {
        close(descriptor);
        errno = EIO;
        return -1;
    }
    unsigned char frame[1024];
    size_t frame_length = 0;
    int result = dbus_read_frame(descriptor, frame, sizeof(frame),
                                 &frame_length) && frame[1] == 1;
    if (result) {
        size_t fields = dbus_align8((size_t)dbus_read_le32(frame + 12));
        size_t body_length = (size_t)dbus_read_le32(frame + 4);
        result = fields <= sizeof(frame) - 16U &&
                 body_length <= sizeof(frame) - 16U - fields &&
                 dbus_send_method_return_frame(
                     descriptor, dbus_read_le32(frame + 8), "s",
                     frame + 16 + fields, body_length);
    }
    close(descriptor);
    if (!result && errno == 0) errno = EIO;
    return result ? 0 : -1;
}

int akaos_dbus_binary_activation_service(void) {
    return dbus_binary_echo_service("com.akaos.AutoEcho");
}

int akaos_dbus_binary_activation_test(void) {
    int caller = dbus_binary_open();
    if (caller < 0) return -1;
    unsigned char body[1024];
    size_t length = 0;
    static const char request[] = "activation-ok";
    unsigned char request_body[64];
    size_t request_length = sizeof(request) - 1U;
    dbus_write_le32(request_body, (uint32_t)request_length);
    memcpy(request_body + 4, request, request_length + 1U);
    int result = dbus_binary_call(caller, 1, "Hello", NULL, NULL, 0, body,
                                  sizeof(body), &length) &&
                 dbus_binary_call_to(caller, 2, "com.akaos.AutoEcho", "Echo",
                                     "s", request_body, request_length + 5U,
                                     body, sizeof(body), &length) &&
                 length == request_length + 5U &&
                 dbus_read_le32(body) == request_length &&
                 memcmp(body + 4, request, request_length + 1U) == 0;
    close(caller);
    if (!result) errno = EIO;
    return result ? 0 : -1;
}

/* Registration probes may run repeatedly against a persistent broker.  Keep
 * each probe's well-known names process-unique so a killed test cannot leave
 * a stale name that makes the next boot look like a duplicate-registration
 * failure. */
static int dbus_test_name(char *buffer, size_t capacity, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    if (prefix_length + 2U > capacity) return 0;
    memcpy(buffer, prefix, prefix_length);
    size_t cursor = prefix_length;
    unsigned value = (unsigned)getpid();
    char digits[16];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    if (cursor + count + 1U > capacity) return 0;
    while (count != 0U) buffer[cursor++] = digits[--count];
    buffer[cursor] = '\0';
    return 1;
}

int akaos_dbus_binary_registration_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char request[320];
    size_t request_length = 0;
    char service_name[64];
    char metadata_name[64];
    if (!dbus_test_name(service_name, sizeof(service_name),
                        "com.akaos.UserEcho") ||
        !dbus_test_name(metadata_name, sizeof(metadata_name),
                        "com.akaos.UserEchoMeta")) {
        close(descriptor);
        errno = EINVAL;
        return -1;
    }
    const char *values[] = {
        service_name, "/bin/hello", "--dbus-activation-service"
    };
    for (unsigned index = 0; index < 3U; ++index) {
        request_length = dbus_align4(request_length);
        size_t value_length = strlen(values[index]);
        if (request_length + value_length + 5U > sizeof(request)) {
            close(descriptor);
            errno = EINVAL;
            return -1;
        }
        dbus_write_le32(request + request_length, (uint32_t)value_length);
        request_length += 4U;
        memcpy(request + request_length, values[index], value_length + 1U);
        request_length += value_length + 1U;
    }
    unsigned char body[64];
    size_t body_length = 0;
    unsigned failure_stage = 1U;
    int result = dbus_binary_call_to(
        descriptor, 1, "org.freedesktop.DBus", "RegisterService", "sss",
        request, request_length, body, sizeof(body), &body_length) &&
        dbus_body_uint_equals(body, body_length, 1U);
    if (result) {
        failure_stage = 2U;
        /* A second registration is rejected while the first definition is
         * active; this protects the bounded table from duplicate entries. */
        result = dbus_binary_call_to(
            descriptor, 2, "org.freedesktop.DBus", "RegisterService", "sss",
            request, request_length, body, sizeof(body), &body_length) &&
            dbus_body_uint_equals(body, body_length, 2U);
    }
    if (result) {
        failure_stage = 3U;
        unsigned char name_body[80];
        size_t name_length = strlen(values[0]);
        dbus_write_le32(name_body, (uint32_t)name_length);
        memcpy(name_body + 4, values[0], name_length + 1U);
        result = dbus_binary_call_to(
            descriptor, 3, "org.freedesktop.DBus", "UnregisterService", "s",
            name_body, name_length + 5U, body, sizeof(body), &body_length) &&
            dbus_body_uint_equals(body, body_length, 1U);
    }
    close(descriptor);
    if (result) {
        /* Exercise the public libc API as well as the legacy three-string
         * wire form.  The metadata form requests bounded restart supervision
         * and a 750 ms activation wait, then verifies duplicate protection. */
        failure_stage = 4U;
        if (akaos_dbus_register_service(
                metadata_name, "/bin/hello", "--dbus-activation-service",
                1, 750U) != 0) {
            result = 0;
        } else {
            failure_stage = 5U;
            errno = 0;
            if (akaos_dbus_register_service(
                    metadata_name, "/bin/hello", "--dbus-activation-service",
                    1, 750U) == 0 || errno != EEXIST)
                result = 0;
            if (result) {
                failure_stage = 6U;
                if (akaos_dbus_unregister_service(metadata_name) != 0)
                    result = 0;
            }
        }
    }
    if (!result) {
        unsigned reply_code = body_length >= 4U ? dbus_read_le32(body) :
                                                   UINT32_MAX;
        fprintf(stderr,
                "D-Bus registration probe failed at stage %u (errno=%d, reply-size=%zu, reply=%u)\n",
                failure_stage, errno, body_length, reply_code);
        if (errno == 0) errno = EIO;
    }
    return result ? 0 : -1;
}

int akaos_dbus_binary_owner_signal_test(void) {
    int observer = dbus_binary_open();
    int owner = observer >= 0 ? dbus_binary_open() : -1;
    unsigned char body[1024];
    size_t length = 0;
    const char requested_name[] = "com.akaos.Signal";
    const char rule[] =
        "type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged'";
    unsigned char string_body[192];
    size_t rule_length = strlen(rule);
    size_t name_length = strlen(requested_name);
    dbus_write_le32(string_body, (uint32_t)rule_length);
    memcpy(string_body + 4, rule, rule_length + 1U);
    int result = observer >= 0 && owner >= 0 &&
                 dbus_binary_call(observer, 1, "Hello", NULL, NULL, 0,
                                  body, sizeof(body), &length) &&
                 dbus_binary_call(observer, 2, "AddMatch", "s", string_body,
                                  rule_length + 5U, body, sizeof(body), &length) &&
                 dbus_binary_call(owner, 1, "Hello", NULL, NULL, 0, body,
                                  sizeof(body), &length);
    dbus_write_le32(string_body, (uint32_t)name_length);
    memcpy(string_body + 4, requested_name, name_length + 1U);
    if (result) {
        result = dbus_binary_call(owner, 2, "RequestName", "s", string_body,
                                  name_length + 5U, body, sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    }
    unsigned char frame[1024];
    size_t frame_length = 0;
    char signal_name[64], old_owner[32], new_owner[32];
    if (result) {
        result = dbus_read_frame(observer, frame, sizeof(frame), &frame_length) &&
                 frame[1] == 4 &&
                 dbus_body_three_strings(
                     frame + 16 + dbus_align8((size_t)dbus_read_le32(frame + 12)),
                     dbus_read_le32(frame + 4), signal_name, sizeof(signal_name),
                     old_owner, sizeof(old_owner), new_owner, sizeof(new_owner)) &&
                 strcmp(signal_name, requested_name) == 0 && old_owner[0] == '\0' &&
                 new_owner[0] == ':';
    }
    if (result) {
        result = dbus_binary_call(owner, 3, "ReleaseName", "s", string_body,
                                  name_length + 5U, body, sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    }
    if (result) {
        result = dbus_read_frame(observer, frame, sizeof(frame), &frame_length) &&
                 frame[1] == 4 &&
                 dbus_body_three_strings(
                     frame + 16 + dbus_align8((size_t)dbus_read_le32(frame + 12)),
                     dbus_read_le32(frame + 4), signal_name, sizeof(signal_name),
                     old_owner, sizeof(old_owner), new_owner, sizeof(new_owner)) &&
                 strcmp(signal_name, requested_name) == 0 && old_owner[0] == ':' &&
                 new_owner[0] == '\0';
    }
    if (owner >= 0) close(owner);
    if (observer >= 0) close(observer);
    if (!result) errno = EIO;
    return result ? 0 : -1;
}

static int dbus_body_names_contains(const unsigned char *body, size_t length,
                                    const char *expected) {
    if (length < 4U) return 0;
    uint32_t array_length = dbus_read_le32(body);
    if ((size_t)array_length + 4U != length) return 0;
    size_t offset = 4;
    size_t expected_length = strlen(expected);
    while (offset < length) {
        offset = dbus_align4(offset);
        if (offset + 5U > length) return 0;
        uint32_t item_length = dbus_read_le32(body + offset);
        offset += 4;
        if ((size_t)item_length + 1U > length - offset) return 0;
        if ((size_t)item_length == expected_length &&
            memcmp(body + offset, expected, expected_length) == 0 &&
            body[offset + item_length] == 0) return 1;
        offset += (size_t)item_length + 1U;
    }
    return 0;
}

static int dbus_body_credentials(const unsigned char *body, size_t length,
                                 uint32_t expected_uid,
                                 uint32_t expected_gid,
                                 uint32_t expected_process_id) {
    if (!body || length < 8U) return 0;
    uint32_t array_length = dbus_read_le32(body);
    size_t entries = dbus_align8(4U);
    if ((size_t)array_length > length - entries) return 0;
    size_t offset = entries;
    size_t end = entries + (size_t)array_length;
    int saw_user = 0;
    int saw_group = 0;
    int saw_process = 0;
    while (offset < end) {
        offset = dbus_align8(offset);
        char key[64];
        if (offset >= end ||
            !dbus_body_string_at(body, end, &offset, key, sizeof(key)) ||
            offset + 3U > end)
            return 0;
        unsigned char signature_length = body[offset++];
        if (signature_length != 1U || offset + 2U > end ||
            body[offset] != 'u' || body[offset + 1U] != 0U)
            return 0;
        offset += 2U; /* variant signature and NUL */
        offset = dbus_align4(offset);
        if (offset + 4U > end) return 0;
        uint32_t value = dbus_read_le32(body + offset);
        offset += 4U;
        if (strcmp(key, "UnixUser") == 0) {
            if (saw_user || value != expected_uid) return 0;
            saw_user = 1;
        } else if (strcmp(key, "UnixGroup") == 0) {
            if (saw_group || value != expected_gid) return 0;
            saw_group = 1;
        } else if (strcmp(key, "ProcessID") == 0) {
            if (saw_process || value != expected_process_id) return 0;
            saw_process = 1;
        } else {
            return 0;
        }
    }
    return offset == end && saw_user && saw_group && saw_process;
}

static size_t dbus_append_string_dictionary_entry(unsigned char *body,
                                                  size_t offset,
                                                  size_t capacity,
                                                  const char *key,
                                                  const char *value) {
    offset = dbus_align8(offset);
    offset = dbus_append_body_string(body, offset, capacity, key);
    if (offset == 0U) return 0U;
    return dbus_append_body_string(body, offset, capacity, value);
}

int akaos_dbus_binary_bus_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char body[1024];
    size_t length = 0;
    const char *failure_stage = "Hello";
    int result = dbus_binary_call(descriptor, 1, "Hello", NULL, NULL, 0,
                                  body, sizeof(body), &length) &&
                 length >= 5U;
    char requested_name[] = "com.akaos.Test";
    unsigned char request_body[64];
    size_t name_length = strlen(requested_name);
    dbus_write_le32(request_body, (uint32_t)name_length);
    memcpy(request_body + 4, requested_name, name_length + 1U);
    if (result) {
        failure_stage = "Hello unique-name reply";
        result = dbus_body_unique_name(body, length);
    }
    if (result) {
        failure_stage = "RequestName";
        result = dbus_binary_call(descriptor, 2, "RequestName", "s",
                                  request_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    }
    if (result) {
        failure_stage = "ListNames";
        result = dbus_binary_call(descriptor, 3, "ListNames", NULL, NULL, 0,
                                  body, sizeof(body), &length) &&
                 dbus_body_names_contains(body, length, requested_name);
    }
    if (result) {
        failure_stage = "ReleaseName";
        result = dbus_binary_call(descriptor, 4, "ReleaseName", "s",
                                  request_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    }
    close(descriptor);
    if (!result) {
        fprintf(stderr, "[D-BUS] Name-service probe failed at %s (errno=%d)\n",
                failure_stage, errno);
        errno = EIO;
    }
    return result ? 0 : -1;
}

int akaos_dbus_binary_standard_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char body[4096];
    size_t length = 0;
    const char *failure_stage = "Hello";
    int result = dbus_binary_call(descriptor, 1U, "Hello", NULL, NULL, 0U,
                                  body, sizeof(body), &length) &&
                 dbus_body_unique_name(body, length);
    char unique[32] = {0};
    if (result) {
        uint32_t unique_length = dbus_read_le32(body);
        if (unique_length >= sizeof(unique)) {
            result = 0;
        } else {
            memcpy(unique, body + 4U, unique_length);
            unique[unique_length] = '\0';
        }
    }
    unsigned char unique_body[64];
    size_t unique_body_length = 0U;
    if (result) {
        unique_body_length = dbus_append_body_string(
            unique_body, 0U, sizeof(unique_body), unique);
        result = unique_body_length != 0U;
    }

    const char requested_name[] = "com.akaos.StandardProbe";
    unsigned char name_body[96];
    size_t name_length = strlen(requested_name);
    dbus_write_le32(name_body, (uint32_t)name_length);
    memcpy(name_body + 4U, requested_name, name_length + 1U);
    if (result)
        result = dbus_binary_call(descriptor, 2U, "RequestName", "s",
                                  name_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    if (result)
        result = dbus_binary_call(descriptor, 3U, "GetNameOwner", "s",
                                  name_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 length == strlen(unique) + 5U &&
                 dbus_read_le32(body) == strlen(unique) &&
                 memcmp(body + 4U, unique, strlen(unique)) == 0 &&
                 body[4U + strlen(unique)] == '\0';
    if (result) {
        failure_stage = "GetConnectionUnixUser";
        result = dbus_binary_call(descriptor, 4U, "GetConnectionUnixUser", "s",
                                  unique_body, unique_body_length, body,
                                  sizeof(body), &length) &&
                  dbus_body_uint_equals(body, length, (uint32_t)geteuid());
    }
    if (result) {
        failure_stage = "GetConnectionUnixProcessID";
        result = dbus_binary_call(descriptor, 5U,
                                  "GetConnectionUnixProcessID", "s",
                                  unique_body, unique_body_length, body,
                                  sizeof(body), &length) &&
                 length == 4U && dbus_read_le32(body) > 0U;
    }
    uint32_t process_id = result ? dbus_read_le32(body) : 0U;
    if (result) {
        failure_stage = "GetConnectionCredentials";
        result = dbus_binary_call(
            descriptor, 6U, "GetConnectionCredentials", "s", unique_body,
            unique_body_length, body, sizeof(body), &length);
        if (result) {
            result = dbus_body_credentials(body, length,
                                           (uint32_t)geteuid(),
                                           (uint32_t)getegid(), process_id);
        }
    }
    if (result) {
        failure_stage = "GetMachineId";
        result = dbus_binary_call(descriptor, 7U, "GetMachineId", NULL, NULL,
                                  0U, body, sizeof(body), &length) &&
                  dbus_body_string_contains(body, length, "akaos");
    }
    if (result) {
        failure_stage = "Introspect";
        result = dbus_binary_call(
            descriptor, 8U, "Introspect", NULL, NULL, 0U, body,
            sizeof(body), &length);
        if (result) {
            int has_owner = dbus_body_string_contains(
                body, length, "GetNameOwner");
            int has_start = dbus_body_string_contains(
                body, length, "StartServiceByName");
            int has_credentials = dbus_body_string_contains(
                body, length, "GetConnectionCredentials");
            result = has_owner && has_start && has_credentials;
        }
    }

    if (result) {
        failure_stage = "GetAllMatchRules";
        result = dbus_binary_call(descriptor, 9U, "GetAllMatchRules", "s",
                                  unique_body, unique_body_length, body,
                                  sizeof(body), &length) && length == 4U &&
                  dbus_read_le32(body) == 0U;
    }
    if (result) {
        failure_stage = "ListQueuedOwners";
        result = dbus_binary_call(descriptor, 10U, "ListQueuedOwners", "s",
                                  name_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_names_contains(body, length, unique);
    }

    unsigned char environment_body[128];
    size_t environment_length = 8U;
    memset(environment_body, 0, sizeof(environment_body));
    environment_length = dbus_append_string_dictionary_entry(
        environment_body, environment_length, sizeof(environment_body),
        "LANG", "C");
    if (environment_length != 0U)
        dbus_write_le32(environment_body,
                        (uint32_t)(environment_length - 8U));
    if (result) {
        failure_stage = "UpdateActivationEnvironment";
        result = environment_length != 0U &&
                 dbus_binary_call(descriptor, 11U,
                                  "UpdateActivationEnvironment", "a{ss}",
                                  environment_body, environment_length, body,
                                  sizeof(body), &length) &&
                 dbus_body_empty(body, length);
    }
    if (result) {
        failure_stage = "ReloadConfig";
        result = dbus_binary_call(descriptor, 12U, "ReloadConfig", NULL, NULL,
                                  0U, body, sizeof(body), &length) &&
                 dbus_body_empty(body, length);
    }

    const char activation_name[] = "com.akaos.AutoEcho";
    unsigned char start_body[96];
    size_t activation_length = strlen(activation_name);
    dbus_write_le32(start_body, (uint32_t)activation_length);
    memcpy(start_body + 4U, activation_name, activation_length + 1U);
    size_t start_offset = dbus_align4(activation_length + 5U);
    dbus_write_le32(start_body + start_offset, 0U);
    if (result) {
        failure_stage = "StartServiceByName";
        result = dbus_binary_call(descriptor, 13U, "StartServiceByName", "su",
                                  start_body, start_offset + 4U, body,
                                  sizeof(body), &length) &&
                 (dbus_body_uint_equals(body, length, 1U) ||
                  dbus_body_uint_equals(body, length, 2U));
    }
    if (result) {
        failure_stage = "Activation Echo";
        static const char activation_request[] = "standard-activation";
        unsigned char request_body[64];
        size_t request_length = sizeof(activation_request) - 1U;
        dbus_write_le32(request_body, (uint32_t)request_length);
        memcpy(request_body + 4U, activation_request, request_length + 1U);
        result = dbus_binary_call_to(
                     descriptor, 14U, activation_name, "Echo", "s",
                     request_body, request_length + 5U, body, sizeof(body),
                     &length) && length == request_length + 5U &&
                 dbus_read_le32(body) == request_length &&
                 memcmp(body + 4U, activation_request, request_length) == 0 &&
                 body[4U + request_length] == '\0';
    }
    if (result) {
        failure_stage = "GetNameOwner after activation";
        int owner_reply = dbus_binary_call(
            descriptor, 15U, "GetNameOwner", "s", name_body,
            name_length + 5U, body, sizeof(body), &length);
        result = owner_reply && length == strlen(unique) + 5U &&
                 dbus_read_le32(body) == strlen(unique) &&
                 memcmp(body + 4U, unique, strlen(unique)) == 0 &&
                 body[4U + strlen(unique)] == '\0';
        if (!result) {
            char actual[32] = "<no reply>";
            if (owner_reply && length >= 5U &&
                dbus_read_le32(body) < sizeof(actual) &&
                (size_t)dbus_read_le32(body) + 5U == length) {
                size_t actual_length = dbus_read_le32(body);
                memcpy(actual, body + 4U, actual_length);
                actual[actual_length] = '\0';
            }
            printf("D-Bus owner diagnostic: expected=%s actual=%s reply=%d bytes=%d\n",
                   unique, actual, owner_reply, (int)length);
        }
    }
    if (result) {
        failure_stage = "ReleaseName";
        result = dbus_binary_call(descriptor, 16U, "ReleaseName", "s",
                                  name_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                  dbus_body_uint_equals(body, length, 1U);
    }
    close(descriptor);
    if (!result) {
        printf("D-Bus standard broker failure at %s\n", failure_stage);
        errno = EIO;
    }
    return result ? 0 : -1;
}

int akaos_dbus_binary_match_test(void) {
    int descriptor = dbus_binary_open();
    if (descriptor < 0) return -1;
    unsigned char body[1024];
    size_t length = 0;
    const char requested_name[] = "com.akaos.Match";
    const char rule[] = "type='signal',interface='com.akaos.Test'";
    unsigned char string_body[128];
    size_t name_length = strlen(requested_name);
    size_t rule_length = strlen(rule);
    dbus_write_le32(string_body, (uint32_t)rule_length);
    memcpy(string_body + 4, rule, rule_length + 1U);
    int result = dbus_binary_call(descriptor, 1, "Hello", NULL, NULL, 0,
                                  body, sizeof(body), &length) &&
                 dbus_body_unique_name(body, length);
    if (result)
        result = dbus_binary_call(descriptor, 2, "AddMatch", "s",
                                  string_body, rule_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_empty(body, length);
    dbus_write_le32(string_body, (uint32_t)name_length);
    memcpy(string_body + 4, requested_name, name_length + 1U);
    if (result)
        result = dbus_binary_call(descriptor, 3, "NameHasOwner", "s",
                                  string_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 0U);
    if (result)
        result = dbus_binary_call(descriptor, 4, "RequestName", "s",
                                  string_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    if (result)
        result = dbus_binary_call(descriptor, 5, "NameHasOwner", "s",
                                  string_body, name_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_uint_equals(body, length, 1U);
    dbus_write_le32(string_body, (uint32_t)rule_length);
    memcpy(string_body + 4, rule, rule_length + 1U);
    if (result)
        result = dbus_binary_call(descriptor, 6, "RemoveMatch", "s",
                                  string_body, rule_length + 5U, body,
                                  sizeof(body), &length) &&
                 dbus_body_empty(body, length);
    close(descriptor);
    if (!result) errno = EIO;
    return result ? 0 : -1;
}

int akaos_dbus_close(int descriptor) {
    return close(descriptor);
}
