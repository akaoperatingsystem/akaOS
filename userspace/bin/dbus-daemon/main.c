#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <akaos/dbus.h>
#include <spawn.h>
#include <akaos/fs.h>
#include <dirent.h>

#define AKAOS_DBUS_PORT 8765U
#define AKAOS_DBUS_MAX_MESSAGE 512U
#define AKAOS_DBUS_MAX_FRAME 4096U
#define AKAOS_DBUS_MAX_NAMES 8U
#define AKAOS_DBUS_MAX_MATCHES 64U
#define AKAOS_DBUS_MAX_MATCH_RULE_LENGTH 512U
#define AKAOS_DBUS_MAX_CLIENTS 8U
#define AKAOS_DBUS_MAX_PENDING 8U
#define AKAOS_DBUS_MAX_ACTIVATIONS 8U
#define AKAOS_DBUS_MAX_UNIX_FDS 4U
#define AKAOS_DBUS_USER_REGISTRY "/run/user/0/dbus-services.conf"
#define AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT 5000U
#define AKAOS_DBUS_MAX_ACTIVATION_TIMEOUT 60000U
#define AKAOS_DBUS_MAX_RESTARTS 3U

static int byte_order_valid(int byte_order);
static uint32_t read_order32(const unsigned char *value, int byte_order);
static size_t align8(size_t value);
static void dbus_close_fds(int *fds, size_t *fd_count);

static char dbus_guid[33] = "00000000000000000000000000000001";

static void initialize_dbus_guid(int session) {
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
    uint64_t high = ((uint64_t)(uint32_t)getpid() << 32) ^
                    (uint64_t)now.tv_sec;
    uint64_t low = (uint64_t)now.tv_nsec ^
                   ((uint64_t)(uint32_t)getuid() << 32) ^
                   ((uint64_t)(session != 0) << 63);
    (void)snprintf(dbus_guid, sizeof(dbus_guid), "%016llx%016llx",
                   (unsigned long long)high, (unsigned long long)low);
}

struct dbus_name_owner {
    int used;
    char name[64];
    char unique[32];
};

static struct dbus_name_owner dbus_names[AKAOS_DBUS_MAX_NAMES];
static unsigned dbus_next_unique = 1U;

static int dbus_trace_enabled(void) {
    struct stat status;
    return stat("/etc/akaos-dbus-trace", &status) == 0;
}

struct dbus_match_rule {
    int used;
    char unique[32];
    char rule[AKAOS_DBUS_MAX_MATCH_RULE_LENGTH];
};

static struct dbus_match_rule dbus_matches[AKAOS_DBUS_MAX_MATCHES];

struct dbus_client_context {
    int used;
    int fd;
    char unique[32];
    struct ucred peer_credentials;
    pthread_t thread;
    pthread_mutex_t write_lock;
};

struct dbus_pending_route {
    int used;
    int delivered;
    int completed;
    int success;
    struct dbus_client_context *caller;
    struct dbus_client_context *target;
    uint32_t target_serial;
    unsigned char frame[AKAOS_DBUS_MAX_FRAME];
    size_t frame_length;
    size_t fd_count;
    int fds[AKAOS_DBUS_MAX_UNIX_FDS];
};

static struct dbus_client_context dbus_clients[AKAOS_DBUS_MAX_CLIENTS];
static struct dbus_pending_route dbus_routes[AKAOS_DBUS_MAX_PENDING];
static pthread_mutex_t dbus_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dbus_route_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dbus_route_condition = PTHREAD_COND_INITIALIZER;
/* Activation entries are loaded from the read-only image registry.  The
 * bounded table keeps concurrent first-use requests deterministic while still
 * allowing desktop services to be added without changing the broker. */
static pthread_mutex_t dbus_activation_lock = PTHREAD_MUTEX_INITIALIZER;

struct dbus_activation_entry {
    int used;
    int started;
    int pid;
    unsigned restart;
    unsigned restart_count;
    unsigned timeout_ms;
    unsigned user_registered;
    char name[64];
    char path[96];
    char argument[96];
};

static struct dbus_activation_entry
    dbus_activation_entries[AKAOS_DBUS_MAX_ACTIVATIONS];
static int dbus_session_bus = 1;

static void broadcast_name_owner_changed(const char *name,
                                         const char *old_owner,
                                         const char *new_owner);

static void usage(void) {
    puts("Usage: dbus-daemon [--session|--system|--version]\n"
         "akaOS native D-Bus bus (line bootstrap + bounded wire/name ABI)");
}

static int send_all(int fd, const char *text) {
    size_t length = strlen(text);
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(fd, text + sent, length - sent, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        sent += (size_t)result;
    }
    return 1;
}

static int send_bytes(int fd, const unsigned char *buffer, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(fd, buffer + sent, length - sent, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        sent += (size_t)result;
    }
    return 1;
}

static void dbus_reply_destination(int fd, char *destination,
                                   size_t capacity) {
    if (!destination || capacity == 0U) return;
    destination[0] = '\0';
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
        const struct dbus_client_context *client = &dbus_clients[index];
        if (!client->used || client->fd != fd) continue;
        size_t length = strlen(client->unique);
        if (length < capacity) memcpy(destination, client->unique, length + 1U);
        break;
    }
    pthread_mutex_unlock(&dbus_state_lock);
}

static int recv_exact(int fd, unsigned char *buffer, size_t length) {
    size_t received = 0;
    while (received < length) {
        ssize_t result = recv(fd, buffer + received, length - received, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        received += (size_t)result;
    }
    return 1;
}

/* Receive the first 16 bytes with recvmsg so descriptor control data cannot
 * be consumed by the broker's ordinary byte reads.  The remainder of this
 * bounded frame follows at the same stream position.  Return 1 for a frame,
 * 0 for orderly disconnect, and -1 for malformed/truncated transport data. */
static int recv_dbus_frame(int fd, unsigned char *frame, size_t capacity,
                           size_t *frame_length, int *fds,
                           size_t *fd_count) {
    if (!frame || capacity < 16U || !frame_length || !fds || !fd_count)
        return -1;
    *frame_length = 0U;
    *fd_count = 0U;
    for (size_t index = 0; index < AKAOS_DBUS_MAX_UNIX_FDS; ++index)
        fds[index] = -1;
    struct iovec vector = {frame, 16U};
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
        received = recvmsg(fd, &message, 0);
        if (received < 0 && (errno == EAGAIN || errno == EINTR))
            (void)sched_yield();
    } while (received < 0 && (errno == EAGAIN || errno == EINTR));
    if (received == 0) return 0;
    if (received < 0) return -1;
    size_t header_received = (size_t)received;
    if (header_received < 16U &&
        !recv_exact(fd, frame + header_received, 16U - header_received)) {
        dbus_close_fds(fds, fd_count);
        return -1;
    }
    int valid = (message.msg_flags & MSG_CTRUNC) == 0;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_len < CMSG_LEN(0U) ||
            header->cmsg_len > message.msg_controllen ||
            header->cmsg_level != SOL_SOCKET ||
            header->cmsg_type != SCM_RIGHTS) {
            valid = 0;
            continue;
        }
        size_t bytes = header->cmsg_len - CMSG_LEN(0U);
        if (bytes % sizeof(int) != 0U) {
            valid = 0;
            continue;
        }
        size_t count = bytes / sizeof(int);
        const int *items = (const int *)CMSG_DATA(header);
        for (size_t index = 0; index < count; ++index) {
            if (*fd_count >= AKAOS_DBUS_MAX_UNIX_FDS) {
                (void)close(items[index]);
                valid = 0;
            } else {
                fds[(*fd_count)++] = items[index];
            }
        }
    }
    if (!valid || !byte_order_valid(frame[0])) {
        dbus_close_fds(fds, fd_count);
        return -1;
    }
    size_t fields_length = read_order32(frame + 12U, frame[0]);
    size_t body_length = read_order32(frame + 4U, frame[0]);
    if (fields_length > capacity - 16U) {
        dbus_close_fds(fds, fd_count);
        return -1;
    }
    size_t padded_fields = align8(fields_length);
    if (padded_fields > capacity - 16U ||
        body_length > capacity - 16U - padded_fields) {
        dbus_close_fds(fds, fd_count);
        return -1;
    }
    size_t tail = padded_fields + body_length;
    if (!recv_exact(fd, frame + 16U, tail)) {
        dbus_close_fds(fds, fd_count);
        return -1;
    }
    *frame_length = 16U + tail;
    return 1;
}

static int recv_line(int fd, char *buffer, size_t capacity) {
    if (!buffer || capacity < 2) return 0;
    size_t length = 0;
    while (length + 1 < capacity) {
        char character;
        ssize_t result = recv(fd, &character, 1, 0);
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

static int recv_bootstrap_line(int fd, unsigned char *buffer, size_t capacity,
                               ssize_t *length_out) {
    if (!buffer || capacity == 0 || !length_out) return 0;
    size_t length = 0;
    while (length < capacity) {
        unsigned char byte;
        ssize_t result = recv(fd, &byte, 1, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        buffer[length++] = byte;
        if (byte == '\n') {
            *length_out = (ssize_t)length;
            return 1;
        }
    }
    return 0;
}

static int auth_hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

/* D-Bus EXTERNAL carries the decimal uid as hex-encoded ASCII. Compare that
 * claim with the kernel-captured AF_UNIX peer uid, never with the claim alone. */
static int auth_external_uid_matches(const char *encoded, uint32_t peer_uid) {
    if (!encoded || !*encoded) return 0;
    char uid_text[16];
    int uid_length = snprintf(uid_text, sizeof(uid_text), "%u",
                              (unsigned)peer_uid);
    if (uid_length <= 0 || (size_t)uid_length >= sizeof(uid_text) ||
        strlen(encoded) != (size_t)uid_length * 2U) return 0;
    for (int index = 0; index < uid_length; ++index) {
        int high = auth_hex_value((unsigned char)encoded[index * 2]);
        int low = auth_hex_value((unsigned char)encoded[index * 2 + 1]);
        if (high < 0 || low < 0 ||
            (char)((high << 4) | low) != uid_text[index]) return 0;
    }
    return 1;
}

static void auth_trim_line(char *line) {
    if (!line) return;
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1U] == '\n' ||
                          line[length - 1U] == '\r'))
        line[--length] = '\0';
}

static int authenticate_standard_client(int client,
                                        const unsigned char *initial,
                                        ssize_t initial_length,
                                        uint32_t peer_uid) {
    if (!initial || initial_length < 2 || initial[0] != 0) return 0;
    char line[AKAOS_DBUS_MAX_MESSAGE];
    size_t length = (size_t)initial_length - 1U;
    if (length >= sizeof(line)) return 0;
    memcpy(line, initial + 1U, length);
    line[length] = '\0';
    auth_trim_line(line);
    if (dbus_trace_enabled())
        fprintf(stderr, "[dbus] authentication request '%s' peer uid=%u\n",
                line, (unsigned)peer_uid);

    for (unsigned attempt = 0; attempt < 4U; ++attempt) {
        static const char auth_prefix[] = "AUTH EXTERNAL ";
        int external_match =
            strncmp(line, auth_prefix, sizeof(auth_prefix) - 1U) == 0 &&
            auth_external_uid_matches(line + sizeof(auth_prefix) - 1U,
                                      peer_uid);
        if (dbus_trace_enabled())
            fprintf(stderr, "[dbus] EXTERNAL identity match=%d\n",
                    external_match);
        if (external_match) {
            char response[96];
            int response_length = snprintf(response, sizeof(response),
                                           "OK %s\r\n", dbus_guid);
            if (response_length <= 0 || !send_all(client, response)) {
                if (dbus_trace_enabled())
                    fprintf(stderr, "[dbus] failed to send OK response\n");
                return 0;
            }
            if (!recv_line(client, line, sizeof(line))) {
                if (dbus_trace_enabled())
                    fprintf(stderr, "[dbus] failed to receive post-auth command (errno=%d)\n",
                            errno);
                return 0;
            }
            auth_trim_line(line);
            if (strcmp(line, "NEGOTIATE_UNIX_FD") == 0) {
                if (dbus_trace_enabled())
                    fprintf(stderr, "[dbus] UNIX_FD negotiation requested\n");
                if (!send_all(client, "AGREE_UNIX_FD\r\n") ||
                    !recv_line(client, line, sizeof(line))) {
                    if (dbus_trace_enabled())
                        fprintf(stderr, "[dbus] UNIX_FD negotiation failed\n");
                    return 0;
                }
                auth_trim_line(line);
            }
            if (dbus_trace_enabled())
                fprintf(stderr, "[dbus] final authentication command '%s'\n", line);
            return strcmp(line, "BEGIN") == 0;
        }

        if (strncmp(line, "AUTH EXTERNAL", 13U) == 0 ||
            strcmp(line, "CANCEL") == 0) {
            if (!send_all(client, "REJECTED EXTERNAL\r\n")) return 0;
        } else {
            if (!send_all(client, "ERROR\r\n")) return 0;
        }
        if (!recv_line(client, line, sizeof(line))) return 0;
        auth_trim_line(line);
    }
    return 0;
}

static int create_unix_listener(const char *path) {
    if (!path || strlen(path) >= UNIX_PATH_MAX) return -1;
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1U);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 8) < 0) {
        close(listener);
        return -1;
    }
    return listener;
}

static void publish_bus_address(const char *file, const char *path) {
    int descriptor = open(file, O_CREAT | O_WRONLY | O_TRUNC);
    if (descriptor < 0) return;
    char address[160];
    int length = snprintf(address, sizeof(address), "unix:path=%s\n", path);
    if (length > 0 && (size_t)length < sizeof(address))
        (void)write(descriptor, address, (size_t)length);
    close(descriptor);
}

static uint32_t read_le32(const unsigned char *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int byte_order_valid(int byte_order) {
    return byte_order == AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN ||
           byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
}

static uint32_t read_order32(const unsigned char *value, int byte_order) {
    if (byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN)
        return ((uint32_t)value[0] << 24U) |
               ((uint32_t)value[1] << 16U) |
               ((uint32_t)value[2] << 8U) | (uint32_t)value[3];
    return read_le32(value);
}

static void write_le32(unsigned char *destination, uint32_t value) {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static size_t align4(size_t value) { return (value + 3U) & ~3U; }
static size_t align8(size_t value) { return (value + 7U) & ~7U; }

static size_t append_uint32_field(unsigned char *header, size_t offset,
                                  unsigned char field_code, uint32_t value) {
    offset = align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1;
    header[offset++] = 'u';
    header[offset++] = 0;
    offset = align4(offset);
    write_le32(header + offset, value);
    return offset + 4;
}

static size_t append_signature_field(unsigned char *header, size_t offset,
                                     unsigned char field_code,
                                     const char *signature) {
    size_t length = strlen(signature);
    offset = align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1;
    header[offset++] = 'g';
    header[offset++] = 0;
    header[offset++] = (unsigned char)length;
    memcpy(header + offset, signature, length);
    offset += length;
    header[offset++] = 0;
    return offset;
}

static size_t append_string_field(unsigned char *header, size_t offset,
                                  unsigned char field_code, char variant_type,
                                  const char *value) {
    size_t length = strlen(value);
    offset = align8(offset);
    header[offset++] = field_code;
    header[offset++] = 1;
    header[offset++] = (unsigned char)variant_type;
    header[offset++] = 0;
    offset = align4(offset);
    write_le32(header + offset, (uint32_t)length);
    offset += 4;
    memcpy(header + offset, value, length);
    offset += length;
    header[offset++] = 0;
    return offset;
}

static int field_string(const unsigned char *frame, size_t fields_end,
                        size_t *offset, unsigned char wanted,
                        char *value, size_t capacity) {
    size_t cursor = align8(*offset);
    if (cursor >= fields_end) {
        *offset = fields_end;
        return 0;
    }
    if (capacity == 0) return 0;
    unsigned char code = frame[cursor++];
    if (cursor + 1 >= fields_end) return 0;
    unsigned char signature_length = frame[cursor++];
    if (signature_length == 0 || signature_length > 8 ||
        cursor + (size_t)signature_length + 1 > fields_end) return 0;
    char signature = (char)frame[cursor];
    cursor += (size_t)signature_length + 1;
    if (signature == 's' || signature == 'o') {
        cursor = align4(cursor);
        if (cursor + 4 > fields_end) return 0;
        uint32_t length = read_le32(frame + cursor);
        cursor += 4;
        if ((size_t)length + 1 > fields_end - cursor ||
            frame[cursor + length] != 0) return 0;
        if (code == wanted && length < capacity) {
            memcpy(value, frame + cursor, length);
            value[length] = '\0';
        }
        *offset = cursor + (size_t)length + 1;
        return code == wanted && length < capacity;
    }
    if (signature == 'g') {
        if (cursor >= fields_end) return 0;
        unsigned char length = frame[cursor++];
        if ((size_t)length + 1 > fields_end - cursor) return 0;
        *offset = cursor + (size_t)length + 1;
        return 0;
    }
    if (signature == 'u' || signature == 'b') {
        cursor = align4(cursor);
        size_t width = 4;
        if (cursor + width > fields_end) return 0;
        *offset = cursor + width;
        return 0;
    }
    if (signature == 'y') {
        if (cursor >= fields_end) return 0;
        *offset = cursor + 1;
        return 0;
    }
    return 0;
}

static int field_signature(const unsigned char *frame, size_t fields_end,
                           size_t *offset, unsigned char wanted,
                           char *value, size_t capacity) {
    size_t cursor = align8(*offset);
    if (cursor >= fields_end) {
        *offset = fields_end;
        return 0;
    }
    if (capacity == 0) return 0;
    unsigned char code = frame[cursor++];
    if (cursor + 1 >= fields_end) return 0;
    unsigned char signature_length = frame[cursor++];
    if (signature_length == 0 || signature_length > 8 ||
        cursor + (size_t)signature_length + 1 > fields_end) return 0;
    char variant = (char)frame[cursor];
    cursor += (size_t)signature_length + 1;
    if (variant == 'g') {
        if (cursor >= fields_end) return 0;
        unsigned char length = frame[cursor++];
        if ((size_t)length + 1 > fields_end - cursor) return 0;
        if (code == wanted && (size_t)length < capacity) {
            memcpy(value, frame + cursor, length);
            value[length] = '\0';
        }
        *offset = cursor + (size_t)length + 1;
        return code == wanted && (size_t)length < capacity;
    }
    if (variant == 's' || variant == 'o') {
        cursor = align4(cursor);
        if (cursor + 4 > fields_end) return 0;
        uint32_t length = read_le32(frame + cursor);
        cursor += 4;
        if ((size_t)length + 1 > fields_end - cursor) return 0;
        *offset = cursor + (size_t)length + 1;
        return 0;
    }
    if (variant == 'u' || variant == 'b') {
        cursor = align4(cursor);
        if (cursor + 4 > fields_end) return 0;
        *offset = cursor + 4;
        return 0;
    }
    if (variant == 'y') {
        if (cursor >= fields_end) return 0;
        *offset = cursor + 1;
        return 0;
    }
    return 0;
}

static int body_string(const unsigned char *body, size_t length,
                       char *value, size_t capacity) {
    if (!body || length < 5 || capacity == 0) return 0;
    uint32_t string_length = read_le32(body);
    if ((size_t)string_length + 5U > length ||
        (size_t)string_length >= capacity ||
        body[4U + string_length] != 0) return 0;
    memcpy(value, body + 4, string_length);
    value[string_length] = '\0';
    return 1;
}

static int body_string_at(const unsigned char *body, size_t length,
                          size_t *offset, char *value, size_t capacity);

static int body_string_uint(const unsigned char *body, size_t length,
                            char *value, size_t capacity, unsigned *number) {
    size_t offset = 0;
    if (!body_string_at(body, length, &offset, value, capacity) || !number)
        return 0;
    offset = align4(offset);
    if (offset + 4U != length) return 0;
    *number = read_le32(body + offset);
    return 1;
}

static int body_string_at(const unsigned char *body, size_t length,
                          size_t *offset, char *value, size_t capacity) {
    if (!body || !offset || *offset > length || capacity == 0) return 0;
    size_t cursor = align4(*offset);
    if (cursor + 5U > length) return 0;
    uint32_t string_length = read_le32(body + cursor);
    cursor += 4U;
    if ((size_t)string_length + 1U > length - cursor ||
        (size_t)string_length >= capacity ||
        body[cursor + string_length] != 0) return 0;
    memcpy(value, body + cursor, string_length);
    value[string_length] = '\0';
    *offset = cursor + (size_t)string_length + 1U;
    return 1;
}

static int body_three_strings(const unsigned char *body, size_t length,
                              char *first, size_t first_capacity,
                              char *second, size_t second_capacity,
                              char *third, size_t third_capacity) {
    size_t offset = 0;
    return body_string_at(body, length, &offset, first, first_capacity) &&
           body_string_at(body, length, &offset, second, second_capacity) &&
           body_string_at(body, length, &offset, third, third_capacity);
}

/* Registration metadata is encoded as three strings followed by restart and
 * timeout uint32 values.
 * Keep the parser separate from the legacy three-string activation test so
 * older clients remain wire-compatible while newer desktop services can ask
 * for bounded restart supervision and an activation wait timeout. */
static int body_three_strings_two_uints(const unsigned char *body,
                                        size_t length,
                                        char *first, size_t first_capacity,
                                        char *second, size_t second_capacity,
                                        char *third, size_t third_capacity,
                                        unsigned *restart,
                                        unsigned *timeout) {
    size_t offset = 0;
    if (!body_string_at(body, length, &offset, first, first_capacity) ||
        !body_string_at(body, length, &offset, second, second_capacity) ||
        !body_string_at(body, length, &offset, third, third_capacity) ||
        !restart || !timeout)
        return 0;
    offset = align4(offset);
    if (offset + 8U != length) return 0;
    *restart = read_le32(body + offset);
    *timeout = read_le32(body + offset + 4U);
    return 1;
}

static int valid_name(const char *name) {
    size_t length = name ? strlen(name) : 0;
    if (length < 1 || length >= 64 || name[0] == '.' ||
        (name[0] >= '0' && name[0] <= '9')) return 0;
    for (size_t index = 0; index < length; ++index) {
        char character = name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' || character == '.'))
            return 0;
    }
    return 1;
}

static char *activation_trim(char *value) {
    if (!value) return value;
    while (*value == ' ' || *value == '\t' || *value == '\r') ++value;
    size_t length = strlen(value);
    while (length > 0 && (value[length - 1U] == ' ' ||
                          value[length - 1U] == '\t' ||
                          value[length - 1U] == '\r'))
        value[--length] = '\0';
    return value;
}

static unsigned parse_activation_uint(const char *value, unsigned fallback,
                                      unsigned maximum) {
    if (!value || !*value) return fallback;
    unsigned result = 0;
    for (const char *cursor = value; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return fallback;
        unsigned digit = (unsigned)(*cursor - '0');
        if (result > (maximum - digit) / 10U) return maximum;
        result = result * 10U + digit;
    }
    return result > maximum ? maximum : result;
}

static int remove_activation_entry(const char *name) {
    if (!name) return 0;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (!entry->used || strcmp(entry->name, name) != 0) continue;
        entry->used = 0;
        entry->started = 0;
        entry->pid = -1;
        entry->restart_count = 0;
        entry->user_registered = 0;
        return 1;
    }
    return 0;
}

static int add_activation_entry(const char *name, const char *path,
                                const char *argument, unsigned restart,
                                unsigned timeout_ms) {
    if (!valid_name(name) || !path || path[0] != '/' || strlen(path) >= 96U ||
        (argument && strlen(argument) >= 96U))
        return 0;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (!entry->used || strcmp(entry->name, name) != 0) continue;
        if (entry->started) return 0;
        entry->restart = restart != 0;
        entry->restart_count = 0;
        entry->timeout_ms = timeout_ms ? timeout_ms : AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT;
        entry->user_registered = 0;
        strncpy(entry->path, path, sizeof(entry->path) - 1U);
        entry->path[sizeof(entry->path) - 1U] = '\0';
        if (argument) {
            strncpy(entry->argument, argument, sizeof(entry->argument) - 1U);
            entry->argument[sizeof(entry->argument) - 1U] = '\0';
        } else {
            entry->argument[0] = '\0';
        }
        return 1;
    }
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used) continue;
        entry->used = 1;
        entry->started = 0;
        entry->pid = -1;
        entry->restart = restart != 0;
        entry->restart_count = 0;
        entry->timeout_ms = timeout_ms ? timeout_ms : AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT;
        strncpy(entry->name, name, sizeof(entry->name) - 1U);
        entry->name[sizeof(entry->name) - 1U] = '\0';
        strncpy(entry->path, path, sizeof(entry->path) - 1U);
        entry->path[sizeof(entry->path) - 1U] = '\0';
        if (argument) {
            strncpy(entry->argument, argument, sizeof(entry->argument) - 1U);
            entry->argument[sizeof(entry->argument) - 1U] = '\0';
        } else {
            entry->argument[0] = '\0';
        }
        return 1;
    }
    return 0;
}

static unsigned load_activation_file(const char *path) {
    int descriptor = open(path, O_RDONLY);
    char data[2048];
    ssize_t count = descriptor >= 0 ? read(descriptor, data, sizeof(data) - 1U) : -1;
    if (descriptor >= 0) close(descriptor);
    unsigned seen = 0;
    if (count <= 0) return 0;
    data[count] = '\0';
    size_t start = 0;
    for (size_t index = 0; index <= (size_t)count; ++index) {
        if (index != (size_t)count && data[index] != '\n') continue;
        size_t length = index - start;
        char line[256];
        if (length >= sizeof(line)) length = sizeof(line) - 1U;
        memcpy(line, data + start, length);
        line[length] = '\0';
        start = index + 1U;
        char *first = activation_trim(line);
        if (first[0] == '\0' || first[0] == '#') continue;
        ++seen;
        if (first[0] == '!') {
            (void)remove_activation_entry(activation_trim(first + 1));
            continue;
        }
        char *second = 0;
        char *third = 0;
        char *fourth = 0;
        char *fifth = 0;
        for (char *cursor = first; *cursor != '\0'; ++cursor) {
            if (*cursor != '|') continue;
            *cursor = '\0';
            if (!second) second = cursor + 1;
            else if (!third) third = cursor + 1;
            else if (!fourth) fourth = cursor + 1;
            else if (!fifth) { fifth = cursor + 1; break; }
        }
        if (!second) continue;
        second = activation_trim(second);
        if (third) third = activation_trim(third);
        if (fourth) fourth = activation_trim(fourth);
        if (fifth) fifth = activation_trim(fifth);
        unsigned restart = parse_activation_uint(fourth, 0U, 1U);
        unsigned timeout = parse_activation_uint(
            fifth, AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT,
            AKAOS_DBUS_MAX_ACTIVATION_TIMEOUT);
        (void)add_activation_entry(activation_trim(first), second, third,
                                   restart, timeout);
    }
    return seen;
}

static unsigned load_service_file(const char *path) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return 0;
    char data[1024];
    ssize_t count = read(descriptor, data, sizeof(data) - 1U);
    close(descriptor);
    if (count <= 0) return 0;
    data[count] = '\0';

    char name[96] = {0};
    char exec[96] = {0};
    char *line = data;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';
        char *trimmed = activation_trim(line);
        if (strncmp(trimmed, "Name=", 5) == 0) {
            strncpy(name, activation_trim(trimmed + 5), sizeof(name) - 1);
        } else if (strncmp(trimmed, "Exec=", 5) == 0) {
            strncpy(exec, activation_trim(trimmed + 5), sizeof(exec) - 1);
        }
        if (!next) break;
        line = next + 1;
    }
    if (name[0] && exec[0]) {
        char *space = strchr(exec, ' ');
        char *arg = NULL;
        if (space) {
            *space = '\0';
            arg = space + 1;
        }
        return add_activation_entry(name, exec, arg, 0, AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT) ? 1 : 0;
    }
    return 0;
}

static unsigned scan_service_directory(const char *dir_path) {
    int dfd = open(dir_path, O_RDONLY);
    if (dfd < 0) return 0;
    struct akaos_dirent entries[16];
    unsigned loaded = 0;
    for (;;) {
        ssize_t count = getdents(dfd, entries, 16);
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; ++i) {
            const char *dname = entries[i].name;
            size_t len = strlen(dname);
            if (len > 8 && strcmp(dname + len - 8, ".service") == 0) {
                char full_path[192];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dname);
                loaded += load_service_file(full_path);
            }
        }
    }
    close(dfd);
    return loaded;
}

static void load_activation_registry(void) {
    unsigned seen = load_activation_file("/etc/dbus-services.conf");
    if (dbus_session_bus) seen += load_activation_file(AKAOS_DBUS_USER_REGISTRY);
    seen += scan_service_directory("/usr/share/dbus-1/services");
    seen += scan_service_directory("/usr/local/share/dbus-1/services");
    if (seen == 0)
        (void)add_activation_entry("com.akaos.AutoEcho", "/bin/hello",
                                   "--dbus-activation-service", 0U,
                                   AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT);
}

static uint32_t request_name(const char *name, const char *unique) {
    if (!valid_name(name)) return 3U;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (!dbus_names[index].used || strcmp(dbus_names[index].name, name) != 0)
            continue;
        uint32_t result = strcmp(dbus_names[index].unique, unique) == 0 ? 4U : 3U;
        pthread_mutex_unlock(&dbus_state_lock);
        return result;
    }
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (dbus_names[index].used) continue;
        dbus_names[index].used = 1;
        strncpy(dbus_names[index].name, name, sizeof(dbus_names[index].name) - 1U);
        dbus_names[index].name[sizeof(dbus_names[index].name) - 1U] = '\0';
        strncpy(dbus_names[index].unique, unique, sizeof(dbus_names[index].unique) - 1U);
        dbus_names[index].unique[sizeof(dbus_names[index].unique) - 1U] = '\0';
        pthread_mutex_unlock(&dbus_state_lock);
        return 1U;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 3U;
}

static uint32_t release_name(const char *name, const char *unique) {
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (!dbus_names[index].used || strcmp(dbus_names[index].name, name) != 0)
            continue;
        if (strcmp(dbus_names[index].unique, unique) != 0) {
            pthread_mutex_unlock(&dbus_state_lock);
            return 2U;
        }
        dbus_names[index].used = 0;
        pthread_mutex_unlock(&dbus_state_lock);
        return 1U;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 2U;
}

static void release_connection_names(const char *unique) {
    char released[AKAOS_DBUS_MAX_NAMES][64];
    unsigned released_count = 0;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (dbus_names[index].used && strcmp(dbus_names[index].unique, unique) == 0) {
            if (released_count < AKAOS_DBUS_MAX_NAMES)
                strncpy(released[released_count++], dbus_names[index].name,
                        sizeof(released[0]) - 1U);
            dbus_names[index].used = 0;
        }
    }
    pthread_mutex_unlock(&dbus_state_lock);
    for (unsigned index = 0; index < released_count; ++index) {
        released[index][sizeof(released[index]) - 1U] = '\0';
        broadcast_name_owner_changed(released[index], unique, "");
    }
}

static int add_match(const char *unique, const char *rule) {
    if (!unique || !rule ||
        strlen(rule) >= AKAOS_DBUS_MAX_MATCH_RULE_LENGTH) return 0;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_MATCHES; ++index) {
        if (dbus_matches[index].used &&
            strcmp(dbus_matches[index].unique, unique) == 0 &&
            strcmp(dbus_matches[index].rule, rule) == 0) {
            pthread_mutex_unlock(&dbus_state_lock);
            return 1;
        }
    }
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_MATCHES; ++index) {
        if (dbus_matches[index].used) continue;
        dbus_matches[index].used = 1;
        strncpy(dbus_matches[index].unique, unique,
                sizeof(dbus_matches[index].unique) - 1U);
        dbus_matches[index].unique[sizeof(dbus_matches[index].unique) - 1U] = '\0';
        strncpy(dbus_matches[index].rule, rule,
                sizeof(dbus_matches[index].rule) - 1U);
        dbus_matches[index].rule[sizeof(dbus_matches[index].rule) - 1U] = '\0';
        pthread_mutex_unlock(&dbus_state_lock);
        return 1;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

static int remove_match(const char *unique, const char *rule) {
    if (!unique || !rule) return 0;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_MATCHES; ++index) {
        if (!dbus_matches[index].used ||
            strcmp(dbus_matches[index].unique, unique) != 0 ||
            strcmp(dbus_matches[index].rule, rule) != 0) continue;
        dbus_matches[index].used = 0;
        pthread_mutex_unlock(&dbus_state_lock);
        return 1;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

static void release_connection_matches(const char *unique) {
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_MATCHES; ++index)
        if (dbus_matches[index].used &&
            strcmp(dbus_matches[index].unique, unique) == 0)
            dbus_matches[index].used = 0;
    pthread_mutex_unlock(&dbus_state_lock);
}

static int name_has_owner(const char *name, const char *unique) {
    if (!name || !unique) return 0;
    pthread_mutex_lock(&dbus_state_lock);
    if (strcmp(name, "org.freedesktop.DBus") == 0 ||
        strcmp(name, unique) == 0) {
        pthread_mutex_unlock(&dbus_state_lock);
        return 1;
    }
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index)
        if (dbus_names[index].used && strcmp(dbus_names[index].name, name) == 0)
        {
            pthread_mutex_unlock(&dbus_state_lock);
            return 1;
        }
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

static int send_method_return(int client, uint32_t reply_serial,
                              const char *signature,
                              const unsigned char *body, size_t body_length) {
    unsigned char frame[AKAOS_DBUS_MAX_FRAME];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 2; /* METHOD_RETURN */
    frame[3] = 1; /* protocol version */
    size_t fields = append_uint32_field(frame + 16, 0, 5, reply_serial);
    char destination[32];
    dbus_reply_destination(client, destination, sizeof(destination));
    if (destination[0] != '\0')
        fields = append_string_field(frame + 16, fields, 6, 's', destination);
    fields = append_string_field(frame + 16, fields, 7, 's', ":1.0");
    if (signature && signature[0] != '\0')
        fields = append_signature_field(frame + 16, fields, 8, signature);
    /* The array length excludes the padding before the 8-byte-aligned body. */
    size_t fields_length = fields;
    size_t padded_fields_length = align8(fields_length);
    if (padded_fields_length > AKAOS_DBUS_MAX_FRAME - 16U ||
        body_length > AKAOS_DBUS_MAX_FRAME - 16U - padded_fields_length)
        return 0;
    if (body_length > 0)
        memcpy(frame + 16 + padded_fields_length, body, body_length);
    write_le32(frame + 4, (uint32_t)body_length);
    write_le32(frame + 8, 1); /* bus serial */
    write_le32(frame + 12, (uint32_t)fields_length);
    size_t total = 16 + padded_fields_length + body_length;
    return send_bytes(client, frame, total);
}

static int send_string_return(int client, uint32_t serial, const char *value) {
    /* Introspection XML grows as the broker exposes more standard interfaces;
     * keep it below the bounded 4 KiB wire-frame ceiling instead of silently
     * dropping a valid reply once the method list expands. */
    unsigned char body[AKAOS_DBUS_MAX_FRAME];
    if (!value) return 0;
    size_t length = strlen(value);
    if (length >= sizeof(body) - 5U) return 0;
    write_le32(body, (uint32_t)length);
    memcpy(body + 4, value, length + 1U);
    return send_method_return(client, serial, "s", body, length + 5U);
}

static int send_error_return(int client, uint32_t serial, const char *name,
                             const char *message) {
    if (!name || !message) return 0;
    unsigned char frame[AKAOS_DBUS_MAX_FRAME];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 3; /* ERROR */
    frame[3] = 1; /* protocol version */
    size_t fields = append_string_field(frame + 16, 0, 4, 's', name);
    fields = append_uint32_field(frame + 16, fields, 5, serial);
    char destination[32];
    dbus_reply_destination(client, destination, sizeof(destination));
    if (destination[0] != '\0')
        fields = append_string_field(frame + 16, fields, 6, 's', destination);
    fields = append_string_field(frame + 16, fields, 7, 's', ":1.0");
    fields = append_signature_field(frame + 16, fields, 8, "s");
    size_t fields_length = fields;
    size_t padded_fields_length = align8(fields_length);
    unsigned char *body = frame + 16 + padded_fields_length;
    size_t message_length = strlen(message);
    if (padded_fields_length > sizeof(frame) - 16U - 5U ||
        message_length >= sizeof(frame) - 16U - padded_fields_length - 5U)
        return 0;
    write_le32(body, (uint32_t)message_length);
    memcpy(body + 4U, message, message_length + 1U);
    write_le32(frame + 4, (uint32_t)(message_length + 5U));
    write_le32(frame + 8, 1U);
    write_le32(frame + 12, (uint32_t)fields_length);
    return send_bytes(client, frame,
                      16U + padded_fields_length + message_length + 5U);
}

static int name_owner(const char *name, const char *requesting_unique,
                      char *owner, size_t owner_capacity) {
    if (!name || !requesting_unique || !owner || owner_capacity == 0) return 0;
    owner[0] = '\0';
    if (strcmp(name, "org.freedesktop.DBus") == 0) {
        if (owner_capacity <= sizeof(":1.0") - 1U) return 0;
        strcpy(owner, ":1.0");
        return 1;
    }
    if (strcmp(name, requesting_unique) == 0) {
        if (strlen(requesting_unique) >= owner_capacity) return 0;
        strcpy(owner, requesting_unique);
        return 1;
    }
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (!dbus_names[index].used || strcmp(dbus_names[index].name, name) != 0)
            continue;
        if (strlen(dbus_names[index].unique) >= owner_capacity) {
            pthread_mutex_unlock(&dbus_state_lock);
            return 0;
        }
        strcpy(owner, dbus_names[index].unique);
        pthread_mutex_unlock(&dbus_state_lock);
        return 1;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

/* Resolve a live bus name to the identity captured by accept(2). The bus
 * daemon is represented by the conventional internal unique name :1.0. */
static int connection_credentials(const char *unique,
                                  struct ucred *credentials) {
    if (!unique || !credentials) return 0;
    if (strcmp(unique, ":1.0") == 0) {
        credentials->pid = (pid_t)getpid();
        credentials->uid = geteuid();
        credentials->gid = getegid();
        return credentials->pid > 0;
    }
    int found = 0;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
        const struct dbus_client_context *client = &dbus_clients[index];
        if (!client->used || strcmp(client->unique, unique) != 0) continue;
        *credentials = client->peer_credentials;
        found = credentials->pid > 0;
        break;
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return found;
}

static const char *dbus_introspection_xml(void) {
    return "<node>"
           "<interface name=\"org.freedesktop.DBus\">"
           "<method name=\"Hello\"><arg direction=\"out\" type=\"s\"/></method>"
           "<method name=\"GetId\"><arg direction=\"out\" type=\"s\"/></method>"
           "<method name=\"GetMachineId\"><arg direction=\"out\" type=\"s\"/></method>"
           "<method name=\"RequestName\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"u\"/></method>"
           "<method name=\"ReleaseName\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"u\"/></method>"
           "<method name=\"NameHasOwner\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"b\"/></method>"
           "<method name=\"GetNameOwner\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"s\"/></method>"
           "<method name=\"GetConnectionUnixUser\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"u\"/></method>"
           "<method name=\"GetConnectionUnixProcessID\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"u\"/></method>"
           "<method name=\"GetConnectionCredentials\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"a{sv}\"/></method>"
           "<method name=\"StartServiceByName\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"in\" type=\"u\"/><arg direction=\"out\" type=\"u\"/></method>"
           "<method name=\"AddMatch\"><arg direction=\"in\" type=\"s\"/></method>"
           "<method name=\"RemoveMatch\"><arg direction=\"in\" type=\"s\"/></method>"
           "<method name=\"GetAllMatchRules\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"as\"/></method>"
           "<method name=\"ListQueuedOwners\"><arg direction=\"in\" type=\"s\"/>"
           "<arg direction=\"out\" type=\"as\"/></method>"
           "<method name=\"UpdateActivationEnvironment\"><arg direction=\"in\" type=\"a{ss}\"/></method>"
           "<method name=\"ReloadConfig\"/>"
           "<method name=\"ListNames\"><arg direction=\"out\" type=\"as\"/></method>"
           "</interface>"
           "<interface name=\"org.freedesktop.DBus.Introspectable\">"
           "<method name=\"Introspect\"><arg direction=\"out\" type=\"s\"/></method>"
           "</interface></node>";
}

static size_t append_body_string(unsigned char *body, size_t offset,
                                 const char *value) {
    size_t length = strlen(value);
    offset = align4(offset);
    write_le32(body + offset, (uint32_t)length);
    offset += 4;
    memcpy(body + offset, value, length);
    offset += length + 1U;
    body[offset - 1U] = 0;
    return offset;
}

static int send_uint_return(int client, uint32_t serial, uint32_t value) {
    unsigned char body[4];
    write_le32(body, value);
    return send_method_return(client, serial, "u", body, sizeof(body));
}

static int send_bool_return(int client, uint32_t serial, int value) {
    unsigned char body[4];
    write_le32(body, value ? 1U : 0U);
    return send_method_return(client, serial, "b", body, sizeof(body));
}

static int send_names_return(int client, uint32_t serial, const char *unique) {
    const char *names[AKAOS_DBUS_MAX_NAMES + 2U];
    unsigned count = 0;
    names[count++] = "org.freedesktop.DBus";
    names[count++] = unique;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index)
        if (dbus_names[index].used) names[count++] = dbus_names[index].name;
    unsigned char body[1024];
    size_t offset = 4;
    for (unsigned index = 0; index < count; ++index) {
        size_t length = strlen(names[index]);
        offset = align4(offset);
        if (length >= 128 || offset + 5U + length > sizeof(body)) return 0;
        write_le32(body + offset, (uint32_t)length);
        offset += 4;
        memcpy(body + offset, names[index], length + 1U);
        offset += length + 1U;
    }
    write_le32(body, (uint32_t)(offset - 4U));
    return send_method_return(client, serial, "as", body, offset);
}

/* Encode a bounded string-array reply.  The helper is shared by the standard
 * broker APIs (`as` is used for both queued owners and match rules), and keeps
 * the array-length/alignment rules in one place instead of relying on the
 * fixed ListNames implementation. */
static int send_string_array_return(int client, uint32_t serial,
                                    const char *const *values,
                                    size_t count) {
    unsigned char body[2048];
    if (count > 32U) return 0;
    size_t offset = 4U;
    for (size_t index = 0U; index < count; ++index) {
        if (!values[index]) return 0;
        offset = align4(offset);
        size_t length = strlen(values[index]);
        if (length >= 128U || offset > sizeof(body) - length - 5U) return 0;
        write_le32(body + offset, (uint32_t)length);
        offset += 4U;
        memcpy(body + offset, values[index], length + 1U);
        offset += length + 1U;
    }
    write_le32(body, (uint32_t)(offset - 4U));
    return send_method_return(client, serial, "as", body, offset);
}

/* Return the credentials dictionary used by common GLib/Qt bus clients.
 * akaOS currently has one uid namespace and does not expose host SCM
 * credentials, so ProcessID is a stable pseudo-id derived from the bus name.
 * Keeping the values typed (u inside v) is more useful than silently
 * stringifying them and gives clients a standard a{sv} shape to consume. */
static int send_credentials_return(int client, uint32_t serial,
                                   const struct ucred *credentials) {
    if (!credentials) return 0;
    unsigned char body[256];
    memset(body, 0, sizeof(body));
    size_t entries = align8(4U);
    size_t offset = entries;
    const char *keys[3] = {"UnixUser", "UnixGroup", "ProcessID"};
    const uint32_t values[3] = {credentials->uid, credentials->gid,
                                (uint32_t)credentials->pid};
    for (size_t index = 0U; index < 3U; ++index) {
        offset = align8(offset);
        offset = append_body_string(body, offset, keys[index]);
        if (offset > sizeof(body) - 7U) return 0;
        body[offset++] = 1U; /* variant signature length */
        body[offset++] = 'u';
        body[offset++] = 0U;
        offset = align4(offset);
        if (offset > sizeof(body) - 4U) return 0;
        write_le32(body + offset, values[index]);
        offset += 4U;
    }
    if (offset < entries || offset - entries > UINT32_MAX) return 0;
    write_le32(body, (uint32_t)(offset - entries));
    return send_method_return(client, serial, "a{sv}", body, offset);
}

static int valid_environment_name(const char *name) {
    if (!name || !name[0] ||
        !((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
        return 0;
    for (const char *cursor = name + 1; *cursor; ++cursor) {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_'))
            return 0;
    }
    return 1;
}

static int body_string_dictionary(const unsigned char *body, size_t length) {
    if (!body || length < 8U) return 0;
    uint32_t array_length = read_le32(body);
    size_t entries = align8(4U);
    if ((size_t)array_length > length - entries) return 0;
    size_t offset = entries;
    size_t end = entries + (size_t)array_length;
    while (offset < end) {
        offset = align8(offset);
        char key[128];
        char value[1024];
        if (offset >= end || !body_string_at(body, end, &offset, key,
                                              sizeof(key)) ||
            !body_string_at(body, end, &offset, value, sizeof(value)) ||
            !valid_environment_name(key) || setenv(key, value, 1) != 0)
            return 0;
    }
    return offset == end;
}

static int send_client_bytes(struct dbus_client_context *client,
                             const unsigned char *frame, size_t length) {
    if (!client || !client->used || client->fd < 0) return 0;
    pthread_mutex_lock(&client->write_lock);
    int result = send_bytes(client->fd, frame, length);
    pthread_mutex_unlock(&client->write_lock);
    return result;
}

static void dbus_close_fds(int *fds, size_t *fd_count) {
    if (!fds || !fd_count) return;
    for (size_t index = 0; index < *fd_count; ++index) {
        if (fds[index] >= 0) (void)close(fds[index]);
        fds[index] = -1;
    }
    *fd_count = 0U;
}

static int send_client_bytes_with_fds(struct dbus_client_context *client,
                                     const unsigned char *frame,
                                     size_t length, const int *fds,
                                     size_t fd_count) {
    if (fd_count == 0U) return send_client_bytes(client, frame, length);
    if (!client || !client->used || client->fd < 0 || !frame || !fds ||
        length == 0U || fd_count > AKAOS_DBUS_MAX_UNIX_FDS)
        return 0;
    pthread_mutex_lock(&client->write_lock);
    struct iovec vector = {(void *)frame, length};
    unsigned char control[CMSG_SPACE(AKAOS_DBUS_MAX_UNIX_FDS * sizeof(int))];
    memset(control, 0, sizeof(control));
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &vector;
    message.msg_iovlen = 1U;
    message.msg_control = control;
    message.msg_controllen = CMSG_SPACE(fd_count * sizeof(int));
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    int result = 0;
    if (header) {
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(fd_count * sizeof(int));
        memcpy(CMSG_DATA(header), fds, fd_count * sizeof(int));
        ssize_t sent = sendmsg(client->fd, &message, 0);
        if (sent > 0) {
            size_t written = (size_t)sent;
            result = 1;
            while (written < length) {
                ssize_t next = send(client->fd, frame + written,
                                    length - written, 0);
                if (next < 0 && (errno == EAGAIN || errno == EINTR)) {
                    (void)sched_yield();
                    continue;
                }
                if (next <= 0) {
                    result = 0;
                    break;
                }
                written += (size_t)next;
            }
        }
    }
    pthread_mutex_unlock(&client->write_lock);
    return result;
}

static int send_name_owner_changed(struct dbus_client_context *client,
                                   const char *name, const char *old_owner,
                                   const char *new_owner) {
    if (!client || !name || !old_owner || !new_owner) return 0;
    unsigned char frame[AKAOS_DBUS_MAX_FRAME];
    memset(frame, 0, sizeof(frame));
    frame[0] = 'l';
    frame[1] = 4; /* SIGNAL */
    frame[3] = 1;
    unsigned char *fields = frame + 16;
    size_t fields_length = append_string_field(
        fields, 0, 1, 'o', "/org/freedesktop/DBus");
    fields_length = append_string_field(
        fields, fields_length, 2, 's', "org.freedesktop.DBus");
    fields_length = append_string_field(
        fields, fields_length, 3, 's', "NameOwnerChanged");
    fields_length = append_string_field(
        fields, fields_length, 4, 's', "org.freedesktop.DBus");
    fields_length = append_signature_field(fields, fields_length, 8, "sss");
    /* Keep the serialized array length separate from the body alignment. */
    size_t padded_fields_length = align8(fields_length);
    unsigned char *body = frame + 16 + padded_fields_length;
    size_t body_length = append_body_string(body, 0, name);
    body_length = append_body_string(body, body_length, old_owner);
    body_length = append_body_string(body, body_length, new_owner);
    if (padded_fields_length > AKAOS_DBUS_MAX_FRAME - 16U ||
        body_length > AKAOS_DBUS_MAX_FRAME - 16U - padded_fields_length)
        return 0;
    write_le32(frame + 4, (uint32_t)body_length);
    write_le32(frame + 8, 1);
    write_le32(frame + 12, (uint32_t)fields_length);
    return send_client_bytes(client, frame,
                             16U + padded_fields_length + body_length);
}

static void broadcast_name_owner_changed(const char *name,
                                         const char *old_owner,
                                         const char *new_owner) {
    if (!name || !old_owner || !new_owner) return;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
        struct dbus_client_context *receiver = &dbus_clients[index];
        if (!receiver->used) continue;
        int matched = 0;
        for (unsigned rule_index = 0;
             rule_index < AKAOS_DBUS_MAX_MATCHES; ++rule_index) {
            struct dbus_match_rule *rule = &dbus_matches[rule_index];
            if (!rule->used || strcmp(rule->unique, receiver->unique) != 0)
                continue;
            if (strstr(rule->rule, "type='signal'") == NULL)
                continue;
            const char *member_filter = strstr(rule->rule, "member='");
            const char *interface_filter = strstr(rule->rule, "interface='");
            if ((!member_filter || strstr(rule->rule, "NameOwnerChanged") != NULL) &&
                (!interface_filter || strstr(rule->rule, "org.freedesktop.DBus") != NULL)) {
                matched = 1;
                break;
            }
        }
        if (matched) (void)send_name_owner_changed(receiver, name, old_owner,
                                                     new_owner);
    }
    pthread_mutex_unlock(&dbus_state_lock);
}

static struct dbus_client_context *find_client_for_name(const char *name) {
    if (!name || name[0] == '\0') return 0;
    pthread_mutex_lock(&dbus_state_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
        struct dbus_client_context *client = &dbus_clients[index];
        if (!client->used || strcmp(client->unique, name) == 0) {
            if (client->used && strcmp(client->unique, name) == 0) {
                pthread_mutex_unlock(&dbus_state_lock);
                return client;
            }
            continue;
        }
    }
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_NAMES; ++index) {
        if (!dbus_names[index].used || strcmp(dbus_names[index].name, name) != 0)
            continue;
        const char *owner = dbus_names[index].unique;
        for (unsigned client_index = 0; client_index < AKAOS_DBUS_MAX_CLIENTS;
             ++client_index) {
            struct dbus_client_context *client = &dbus_clients[client_index];
            if (client->used && strcmp(client->unique, owner) == 0) {
                pthread_mutex_unlock(&dbus_state_lock);
                return client;
            }
        }
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

static int activate_service(const char *destination) {
    if (!destination || destination[0] == '\0') return 0;
    char path[96];
    char argument[96];
    struct dbus_activation_entry *selected = 0;
    pthread_mutex_lock(&dbus_activation_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used && strcmp(entry->name, destination) == 0) {
            selected = entry;
            break;
        }
    }
    if (!selected) {
        pthread_mutex_unlock(&dbus_activation_lock);
        return 0;
    }
    if (selected->started || selected->pid > 0) {
        pthread_mutex_unlock(&dbus_activation_lock);
        return 1;
    }
    selected->started = 1;
    selected->pid = -1;
    selected->restart_count = 0;
    strncpy(path, selected->path, sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    strncpy(argument, selected->argument, sizeof(argument) - 1U);
    argument[sizeof(argument) - 1U] = '\0';
    pthread_mutex_unlock(&dbus_activation_lock);

    char *arguments[] = {path, argument[0] != '\0' ? argument : NULL, NULL};
    posix_spawn_file_actions_t file_actions;
    int spawn_error = posix_spawn_file_actions_init(&file_actions);
    int actions_initialized = spawn_error == 0;
    if (spawn_error == 0)
        spawn_error = posix_spawn_file_actions_addclosefrom_np(&file_actions, 3);
    pid_t child_pid = -1;
    if (spawn_error == 0)
        spawn_error = posix_spawn(&child_pid, path, &file_actions, NULL,
                                  arguments, NULL);
    if (actions_initialized)
        (void)posix_spawn_file_actions_destroy(&file_actions);
    int pid = spawn_error == 0 ? (int)child_pid : -1;
    if (pid >= 0) {
        pthread_mutex_lock(&dbus_activation_lock);
        selected->pid = pid;
        pthread_mutex_unlock(&dbus_activation_lock);
        return 1;
    }

    pthread_mutex_lock(&dbus_activation_lock);
    if (spawn_error != 0) errno = spawn_error;
    selected->started = 0;
    selected->pid = -1;
    pthread_mutex_unlock(&dbus_activation_lock);
    return 0;
}

static unsigned activation_wait_budget(const char *destination) {
    unsigned timeout = AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT;
    pthread_mutex_lock(&dbus_activation_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used && destination && strcmp(entry->name, destination) == 0) {
            timeout = entry->timeout_ms;
            break;
        }
    }
    pthread_mutex_unlock(&dbus_activation_lock);
    /* One scheduler yield is intentionally a short bounded wait. */
    unsigned budget = timeout / 10U;
    if (budget < 100U) budget = 100U;
    if (budget > 6000U) budget = 6000U;
    return budget;
}

static int persist_activation_line(const char *line) {
    if (!dbus_session_bus || !line) return 0;
    int descriptor = open(AKAOS_DBUS_USER_REGISTRY,
                          O_WRONLY | O_CREAT | O_APPEND);
    if (descriptor < 0) {
        fprintf(stderr, "dbus-daemon: activation registry open failed (errno=%d)\n",
                errno);
        return 0;
    }
    size_t length = strlen(line);
    ssize_t written = write(descriptor, line, length);
    int result = written == (ssize_t)length;
    if (!result)
        fprintf(stderr,
                "dbus-daemon: activation registry write failed (wrote=%d, expected=%d, errno=%d)\n",
                (int)written, (int)length, errno);
    close(descriptor);
    return result;
}

static uint32_t register_service(const char *name, const char *path,
                                 const char *argument, unsigned restart,
                                 unsigned timeout_ms) {
    if (!dbus_session_bus || !valid_name(name) || !path || path[0] != '/')
        return 3U;
    if (!argument) argument = "";
    if (strlen(path) >= 96U || strlen(argument) >= 96U) return 3U;
    if (timeout_ms == 0U) timeout_ms = AKAOS_DBUS_DEFAULT_ACTIVATION_TIMEOUT;
    if (timeout_ms > AKAOS_DBUS_MAX_ACTIVATION_TIMEOUT)
        timeout_ms = AKAOS_DBUS_MAX_ACTIVATION_TIMEOUT;
    pthread_mutex_lock(&dbus_activation_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used && entry->user_registered &&
            strcmp(entry->name, name) == 0) {
            pthread_mutex_unlock(&dbus_activation_lock);
            return 2U;
        }
    }
    int added = add_activation_entry(name, path, argument, restart, timeout_ms);
    if (added) {
        for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
            struct dbus_activation_entry *entry = &dbus_activation_entries[index];
            if (entry->used && strcmp(entry->name, name) == 0) {
                entry->user_registered = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&dbus_activation_lock);
    if (!added) return 2U;
    char line[320];
    int length = snprintf(line, sizeof(line), "%s|%s|%s|%u|%u\n",
                          name, path, argument, restart ? 1U : 0U,
                          timeout_ms);
    if (length <= 0 || (size_t)length >= sizeof(line) ||
        !persist_activation_line(line)) return 4U;
    return 1U;
}

static uint32_t unregister_service(const char *name) {
    if (!dbus_session_bus || !valid_name(name)) return 3U;
    int pid = -1;
    int started = 0;
    pthread_mutex_lock(&dbus_activation_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used && strcmp(entry->name, name) == 0) {
            pid = entry->pid;
            started = entry->started;
            break;
        }
    }
    if (pid > 0 && started) {
        /* An activation child can be reaped by the supervisor (or by a
         * previous cleanup path) before the registry entry is revisited.  Do
         * not leave that stale PID blocking an otherwise valid unregister. */
        int status = 0;
        pthread_mutex_unlock(&dbus_activation_lock);
        int waited = waitpid(pid, &status, WNOHANG);
        pthread_mutex_lock(&dbus_activation_lock);
        for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
            struct dbus_activation_entry *entry = &dbus_activation_entries[index];
            if (!entry->used || strcmp(entry->name, name) != 0 ||
                entry->pid != pid) continue;
            if (waited == 0) {
                pthread_mutex_unlock(&dbus_activation_lock);
                return 5U;
            }
            entry->started = 0;
            entry->pid = -1;
            break;
        }
    } else if (pid > 0) {
        /* A supervisor that already observed exit clears `started` before
         * its next PID update.  Treat that positive PID as stale too. */
        for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
            struct dbus_activation_entry *entry = &dbus_activation_entries[index];
            if (entry->used && strcmp(entry->name, name) == 0) {
                entry->pid = -1;
                break;
            }
        }
    }
    int removed = remove_activation_entry(name);
    pthread_mutex_unlock(&dbus_activation_lock);
    if (!removed) return 2U;
    char line[96];
    int length = snprintf(line, sizeof(line), "!%s\n", name);
    if (length <= 0 || (size_t)length >= sizeof(line) ||
        !persist_activation_line(line)) return 4U;
    return 1U;
}

static void *activation_supervisor(void *argument) {
    (void)argument;
    for (;;) {
        for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
            int pid = -1;
            int should_restart = 0;
            char path[96];
            char service_argument[96];
            pthread_mutex_lock(&dbus_activation_lock);
            struct dbus_activation_entry *entry = &dbus_activation_entries[index];
            if (entry->used && entry->pid > 0) {
                pid = entry->pid;
                if (entry->started && entry->restart &&
                    entry->restart_count < AKAOS_DBUS_MAX_RESTARTS) {
                    should_restart = 1;
                    ++entry->restart_count;
                }
                strncpy(path, entry->path, sizeof(path) - 1U);
                path[sizeof(path) - 1U] = '\0';
                strncpy(service_argument, entry->argument,
                        sizeof(service_argument) - 1U);
                service_argument[sizeof(service_argument) - 1U] = '\0';
            }
            pthread_mutex_unlock(&dbus_activation_lock);
            if (pid <= 0) continue;
            int status = 0;
            int result = waitpid(pid, &status, WNOHANG);
            if (result != pid) continue;
            pthread_mutex_lock(&dbus_activation_lock);
            entry = &dbus_activation_entries[index];
            if (entry->used && entry->pid == pid) {
                entry->started = 0;
                entry->pid = -1;
            }
            pthread_mutex_unlock(&dbus_activation_lock);
            if (should_restart) {
                char *arguments[] = {path,
                    service_argument[0] ? service_argument : NULL, NULL};
                int replacement = spawn(path, arguments);
                if (replacement >= 0) {
                    pthread_mutex_lock(&dbus_activation_lock);
                    entry = &dbus_activation_entries[index];
                    if (entry->used && !entry->started && entry->pid < 0) {
                        entry->started = 1;
                        entry->pid = replacement;
                    }
                    pthread_mutex_unlock(&dbus_activation_lock);
                }
            }
        }
        (void)usleep(10000);
    }
}

static int queue_route(struct dbus_client_context *caller,
                       struct dbus_client_context *target,
                       const unsigned char *frame, size_t frame_length,
                       uint32_t target_serial, const int *fds,
                       size_t fd_count) {
    if (!caller || !target || !frame || frame_length > AKAOS_DBUS_MAX_FRAME ||
        fd_count > AKAOS_DBUS_MAX_UNIX_FDS || (fd_count != 0U && !fds))
        return 0;
    int route_fds[AKAOS_DBUS_MAX_UNIX_FDS] = {-1, -1, -1, -1};
    size_t route_fd_count = 0U;
    for (; route_fd_count < fd_count; ++route_fd_count) {
        route_fds[route_fd_count] = dup(fds[route_fd_count]);
        if (route_fds[route_fd_count] < 0) {
            dbus_close_fds(route_fds, &route_fd_count);
            return 0;
        }
    }
    pthread_mutex_lock(&dbus_route_lock);
    struct dbus_pending_route *route = 0;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_PENDING; ++index) {
        if (!dbus_routes[index].used) {
            route = &dbus_routes[index];
            break;
        }
    }
    if (!route) {
        pthread_mutex_unlock(&dbus_route_lock);
        dbus_close_fds(route_fds, &route_fd_count);
        return 0;
    }
    route->used = 1;
    route->delivered = 0;
    route->completed = 0;
    route->success = 0;
    route->caller = caller;
    route->target = target;
    route->target_serial = target_serial;
    route->frame_length = frame_length;
    memcpy(route->frame, frame, frame_length);
    route->fd_count = route_fd_count;
    for (size_t index = 0; index < route_fd_count; ++index) {
        route->fds[index] = route_fds[index];
        route_fds[index] = -1;
    }
    route_fd_count = 0U;
    pthread_cond_broadcast(&dbus_route_condition);
    while (route->used && !route->completed)
        (void)pthread_cond_wait(&dbus_route_condition, &dbus_route_lock);
    int success = route->completed && route->success;
    dbus_close_fds(route->fds, &route->fd_count);
    route->used = 0;
    pthread_mutex_unlock(&dbus_route_lock);
    return success;
}

static void fail_routes_for_client(struct dbus_client_context *client) {
    pthread_mutex_lock(&dbus_route_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_PENDING; ++index) {
        struct dbus_pending_route *route = &dbus_routes[index];
        /* A successful reply can race with the target disconnecting (common
         * for one-shot activated services).  Never replace an already
         * completed success with disconnect failure. */
        if (!route->used || route->completed ||
            (route->caller != client && route->target != client))
            continue;
        route->completed = 1;
        route->success = 0;
        pthread_cond_broadcast(&dbus_route_condition);
    }
    pthread_mutex_unlock(&dbus_route_lock);
}

static int deliver_queued_route(struct dbus_client_context *target) {
    unsigned char frame[AKAOS_DBUS_MAX_FRAME];
    size_t frame_length = 0;
    int fds[AKAOS_DBUS_MAX_UNIX_FDS] = {-1, -1, -1, -1};
    size_t fd_count = 0U;
    struct dbus_pending_route *route = 0;
    pthread_mutex_lock(&dbus_route_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_PENDING; ++index) {
        if (dbus_routes[index].used && dbus_routes[index].target == target &&
            !dbus_routes[index].delivered) {
            route = &dbus_routes[index];
            frame_length = route->frame_length;
            memcpy(frame, route->frame, frame_length);
            fd_count = route->fd_count;
            for (size_t index = 0; index < fd_count; ++index)
                fds[index] = route->fds[index];
            route->delivered = 1;
            break;
        }
    }
    pthread_mutex_unlock(&dbus_route_lock);
    if (!route) return 0;
    if (send_client_bytes_with_fds(target, frame, frame_length, fds,
                                   fd_count)) return 1;
    pthread_mutex_lock(&dbus_route_lock);
    route->completed = 1;
    route->success = 0;
    pthread_cond_broadcast(&dbus_route_condition);
    pthread_mutex_unlock(&dbus_route_lock);
    return 1;
}

static int forward_route_reply(struct dbus_client_context *target,
                               uint32_t target_serial,
                               const unsigned char *frame, size_t frame_length,
                               const int *fds, size_t fd_count) {
    pthread_mutex_lock(&dbus_route_lock);
    struct dbus_pending_route *route = 0;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_PENDING; ++index) {
        if (dbus_routes[index].used && dbus_routes[index].target == target &&
            dbus_routes[index].delivered &&
            dbus_routes[index].target_serial == target_serial) {
            route = &dbus_routes[index];
            break;
        }
    }
    if (!route) {
        pthread_mutex_unlock(&dbus_route_lock);
        return 0;
    }
    struct dbus_client_context *caller = route->caller;
    int sent = send_client_bytes_with_fds(caller, frame, frame_length,
                                          fds, fd_count);
    route->completed = 1;
    route->success = sent;
    pthread_cond_broadcast(&dbus_route_condition);
    pthread_mutex_unlock(&dbus_route_lock);
    return 1;
}

static int find_string_field(const unsigned char *fields, size_t fields_length,
                             unsigned char wanted, char *value,
                             size_t capacity) {
    size_t offset = 0;
    int found = 0;
    while (offset < fields_length) {
        size_t before = offset;
        if (field_string(fields, fields_length, &offset, wanted, value, capacity))
            found = 1;
        if (offset <= before) return -1;
    }
    return found;
}

static int find_signature_field(const unsigned char *fields, size_t fields_length,
                                unsigned char wanted, char *value,
                                size_t capacity) {
    size_t offset = 0;
    int found = 0;
    while (offset < fields_length) {
        size_t before = offset;
        if (field_signature(fields, fields_length, &offset, wanted, value,
                            capacity)) found = 1;
        if (offset <= before) return -1;
    }
    return found;
}

static int find_uint32_field(const unsigned char *fields,
                             size_t fields_length, unsigned char wanted,
                             uint32_t *value) {
    if (!fields || !value) return -1;
    size_t offset = 0U;
    int found = 0;
    while (offset < fields_length) {
        size_t cursor = align8(offset);
        if (cursor == fields_length) break;
        if (cursor > fields_length || fields_length - cursor < 4U)
            return -1;
        unsigned char code = fields[cursor++];
        unsigned char signature_length = fields[cursor++];
        if (signature_length != 1U || fields[cursor + 1U] != 0U)
            return -1;
        unsigned char type = fields[cursor];
        cursor += 2U;
        size_t value_offset = cursor;
        size_t width = 0U;
        switch (type) {
        case 'y': width = 1U; break;
        case 'n': case 'q':
            value_offset = (value_offset + 1U) & ~(size_t)1U;
            width = 2U;
            break;
        case 'b': case 'i': case 'u':
            value_offset = (value_offset + 3U) & ~(size_t)3U;
            width = 4U;
            break;
        case 'x': case 't': case 'd':
            value_offset = align8(value_offset);
            width = 8U;
            break;
        case 's': case 'o': {
            value_offset = (value_offset + 3U) & ~(size_t)3U;
            if (value_offset > fields_length ||
                fields_length - value_offset < 4U)
                return -1;
            uint32_t length = read_le32(fields + value_offset);
            size_t string_offset = value_offset + 4U;
            if ((size_t)length >= fields_length - string_offset ||
                fields[string_offset + length] != 0U)
                return -1;
            offset = string_offset + (size_t)length + 1U;
            continue;
        }
        case 'g':
            if (value_offset >= fields_length) return -1;
            width = (size_t)fields[value_offset] + 2U;
            break;
        default:
            return -1;
        }
        if (value_offset > fields_length ||
            width > fields_length - value_offset)
            return -1;
        if (code == wanted && type == 'u') {
            *value = read_le32(fields + value_offset);
            found = 1;
        }
        offset = value_offset + width;
    }
    return found;
}

static int dbus_validate_frame_fds(const unsigned char *frame,
                                   size_t fd_count) {
    if (!frame || fd_count > AKAOS_DBUS_MAX_UNIX_FDS) return 0;
    size_t fields_length = read_le32(frame + 12U);
    uint32_t declared_count = 0U;
    int found = find_uint32_field(frame + 16U, fields_length, 9U,
                                  &declared_count);
    if (found < 0) return 0;
    if (fd_count != 0U && !found) return 0;
    return (found ? (size_t)declared_count : 0U) == fd_count;
}

static int handle_binary_client(struct dbus_client_context *context) {
    if (!context) return 0;
    int client = context->fd;
    const char *unique = context->unique;
    unsigned binary_frames = 0U;
    for (;;) {
        (void)deliver_queued_route(context);
        struct pollfd descriptor = {client, POLLIN, 0};
        int ready = poll(&descriptor, 1, 50);
        if (ready < 0) {
            return 0;
        }
        if (ready == 0) continue;
        unsigned char frame[AKAOS_DBUS_MAX_FRAME];
        int received_fds[AKAOS_DBUS_MAX_UNIX_FDS] = {-1, -1, -1, -1};
        size_t received_fd_count = 0U;
        size_t frame_length = 0U;
        int read_status = recv_dbus_frame(client, frame, sizeof(frame),
                                          &frame_length, received_fds,
                                          &received_fd_count);
        if (read_status == 0) {
            if (dbus_trace_enabled())
                fprintf(stderr, "[dbus] standard client closed after %u frames\n",
                        binary_frames);
            return 1;
        }
        if (read_status < 0) {
            fprintf(stderr,
                    "dbus-daemon: frame receive failed (errno=%d)\n", errno);
            return 0;
        }
        ++binary_frames;
        if (dbus_trace_enabled())
            fprintf(stderr, "[dbus] received binary frame type=%u serial=%u bytes=%zu\n",
                    (unsigned)frame[1], (unsigned)read_le32(frame + 8U),
                    frame_length);
        uint32_t body_length = read_order32(frame + 4U, frame[0]);
        uint32_t fields_length = read_order32(frame + 12U, frame[0]);
        if (akaos_dbus_message_normalize_little_endian(
                frame, frame_length) != 0) {
            fprintf(stderr, "dbus-daemon: malformed D-Bus frame\n");
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        if (!dbus_validate_frame_fds(frame, received_fd_count)) {
            fprintf(stderr,
                    "dbus-daemon: UNIX_FDS count mismatch (received=%u)\n",
                    (unsigned)received_fd_count);
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        uint32_t serial = read_le32(frame + 8);
        if (frame[1] == 2 || frame[1] == 3) {
            int forwarded = forward_route_reply(
                context, serial, frame, frame_length, received_fds,
                received_fd_count);
            dbus_close_fds(received_fds, &received_fd_count);
            if (!forwarded) {
                return 0;
            }
            continue;
        }
        if (frame[1] != 1 && frame[1] != 4) {
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        char member[96] = {0};
        char interface[96] = {0};
        char destination[96] = {0};
        char signature[16] = {0};
        if (find_string_field(frame + 16, (size_t)fields_length, 3,
                              member, sizeof(member)) < 0 ||
            find_string_field(frame + 16, (size_t)fields_length, 2,
                              interface, sizeof(interface)) < 0 ||
            find_string_field(frame + 16, (size_t)fields_length, 6,
                              destination, sizeof(destination)) < 0 ||
            find_signature_field(frame + 16, (size_t)fields_length, 8,
                                 signature, sizeof(signature)) < 0) {
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        if (dbus_trace_enabled())
            fprintf(stderr, "[dbus] method member='%s' interface='%s' destination='%s' signature='%s'\n",
                    member, interface, destination, signature);
        if (frame[1] == 4) {
            /* Signals are delivered to clients that registered a compatible
             * bounded match rule.  The payload is copied as-is. */
            pthread_mutex_lock(&dbus_state_lock);
            for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
                struct dbus_client_context *receiver = &dbus_clients[index];
                if (!receiver->used || receiver == context) continue;
                int matched = 0;
                for (unsigned rule_index = 0;
                     rule_index < AKAOS_DBUS_MAX_MATCHES; ++rule_index) {
                    struct dbus_match_rule *rule = &dbus_matches[rule_index];
                    if (!rule->used || strcmp(rule->unique, receiver->unique) != 0)
                        continue;
                    if (strstr(rule->rule, "type='signal'") != NULL &&
                        (strstr(rule->rule, "member='") == NULL ||
                         strstr(rule->rule, member) != NULL) &&
                        (strstr(rule->rule, "interface='") == NULL ||
                         (interface[0] != '\0' &&
                          strstr(rule->rule, interface) != NULL))) {
                        matched = 1;
                        break;
                    }
                }
                if (matched)
                    (void)send_client_bytes_with_fds(
                        receiver, frame, frame_length, received_fds,
                        received_fd_count);
            }
            pthread_mutex_unlock(&dbus_state_lock);
            dbus_close_fds(received_fds, &received_fd_count);
            continue;
        }
        if (member[0] == '\0') {
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        size_t padded_fields = align8((size_t)fields_length);
        const unsigned char *body = frame + 16 + padded_fields;
        size_t body_size = (size_t)body_length;
        int sent = 0;
        if (destination[0] != '\0' &&
            strcmp(destination, "org.freedesktop.DBus") != 0) {
            struct dbus_client_context *target = find_client_for_name(destination);
            if (!target && activate_service(destination)) {
                /* Give the newly spawned provider a bounded scheduling window
                 * to connect, complete Hello, and claim its well-known name.
                 * The original method remains queued until that happens. */
                unsigned wait_budget = activation_wait_budget(destination);
                for (unsigned wait = 0; wait < wait_budget && !target; ++wait) {
                    (void)sched_yield();
                    target = find_client_for_name(destination);
                }
            }
            int queued = target &&
                queue_route(context, target, frame, frame_length, serial,
                            received_fds, received_fd_count);
            dbus_close_fds(received_fds, &received_fd_count);
            if (!queued) {
                if (!target) {
                    sent = send_error_return(
                        client, serial,
                        "org.freedesktop.DBus.Error.ServiceUnknown",
                        "The requested D-Bus destination has no registered owner");
                    if (dbus_trace_enabled())
                        fprintf(stderr,
                                "[dbus] unknown destination '%s' error reply sent=%d\n",
                                destination, sent);
                    if (sent) continue;
                }
                return 0;
            }
            continue;
        }
        /* Built-in bus methods have no `h` payload support yet.  Reject
         * attached files explicitly so callers never observe silent loss. */
        if (received_fd_count != 0U) {
            dbus_close_fds(received_fds, &received_fd_count);
            return 0;
        }
        if (strcmp(member, "Hello") == 0) {
            sent = send_string_return(client, serial, unique);
            if (dbus_trace_enabled())
                fprintf(stderr, "[dbus] Hello reply sent=%d unique='%s'\n",
                        sent, unique);
        } else if (strcmp(member, "GetId") == 0) {
            sent = send_string_return(client, serial, dbus_guid);
        } else if (strcmp(member, "GetMachineId") == 0) {
            sent = send_string_return(client, serial, dbus_guid);
        } else if (strcmp(member, "RequestName") == 0 &&
                   (strcmp(signature, "s") == 0 ||
                    strcmp(signature, "su") == 0)) {
            char name[64];
            unsigned flags = 0U;
            int parsed = strcmp(signature, "su") == 0
                ? body_string_uint(body, body_size, name, sizeof(name), &flags)
                : body_string(body, body_size, name, sizeof(name));
            if (!parsed) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "RequestName requires a bus name and flags");
            } else {
                uint32_t result = request_name(name, unique);
                /* The bounded broker currently grants or rejects immediately;
                 * it does not implement the optional owner queue/replacement
                 * policy encoded by flags. */
                (void)flags;
                sent = send_uint_return(client, serial, result);
                if (sent && result == 1U)
                    broadcast_name_owner_changed(name, "", unique);
                if (dbus_trace_enabled())
                    fprintf(stderr,
                            "[dbus] RequestName name='%s' flags=%u result=%u sent=%d\n",
                            name, flags, result, sent);
            }
        } else if (strcmp(member, "RegisterService") == 0 &&
                   (strcmp(signature, "sss") == 0 ||
                    strcmp(signature, "sssuu") == 0)) {
            char service_name[64];
            char service_path[96];
            char service_argument[96];
            unsigned restart = 0U;
            unsigned timeout = 0U;
            int parsed = strcmp(signature, "sssuu") == 0
                ? body_three_strings_two_uints(
                    body, body_size, service_name, sizeof(service_name),
                    service_path, sizeof(service_path), service_argument,
                    sizeof(service_argument), &restart, &timeout)
                : body_three_strings(
                    body, body_size, service_name, sizeof(service_name),
                    service_path, sizeof(service_path), service_argument,
                    sizeof(service_argument));
            uint32_t result = parsed
                ? register_service(service_name, service_path,
                                   service_argument, restart, timeout)
                : 3U;
            sent = send_uint_return(client, serial, result);
        } else if (strcmp(member, "UnregisterService") == 0 &&
                   strcmp(signature, "s") == 0) {
            char service_name[64];
            uint32_t result = body_string(body, body_size, service_name,
                                          sizeof(service_name))
                ? unregister_service(service_name) : 3U;
            sent = send_uint_return(client, serial, result);
        } else if (strcmp(member, "ReleaseName") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            uint32_t result = body_string(body, body_size, name, sizeof(name))
                                  ? release_name(name, unique)
                                  : 2U;
            sent = send_uint_return(client, serial, result);
            if (sent && result == 1U)
                broadcast_name_owner_changed(name, unique, "");
        } else if (strcmp(member, "NameHasOwner") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            sent = body_string(body, body_size, name, sizeof(name)) &&
                   send_bool_return(client, serial, name_has_owner(name, unique));
        } else if (strcmp(member, "GetNameOwner") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "GetNameOwner requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                sent = send_string_return(client, serial, owner);
            }
        } else if (strcmp(member, "GetConnectionUnixUser") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            struct ucred credentials;
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "GetConnectionUnixUser requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner)) ||
                       !connection_credentials(owner, &credentials)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                sent = send_uint_return(client, serial, credentials.uid);
            }
        } else if (strcmp(member, "GetConnectionUnixProcessID") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            struct ucred credentials;
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "GetConnectionUnixProcessID requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner)) ||
                       !connection_credentials(owner, &credentials)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                sent = send_uint_return(client, serial,
                                        (uint32_t)credentials.pid);
            }
        } else if (strcmp(member, "GetConnectionCredentials") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            struct ucred credentials;
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "GetConnectionCredentials requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner)) ||
                       !connection_credentials(owner, &credentials)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                sent = send_credentials_return(client, serial, &credentials);
            }
        } else if (strcmp(member, "StartServiceByName") == 0 &&
                   strcmp(signature, "su") == 0) {
            char name[64];
            unsigned flags = 0U;
            (void)flags;
            if (!body_string_uint(body, body_size, name, sizeof(name), &flags)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "StartServiceByName requires a name and flags");
            } else if (name_has_owner(name, unique)) {
                sent = send_uint_return(client, serial, 2U);
            } else if (activate_service(name)) {
                sent = send_uint_return(client, serial, 1U);
            } else {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.ServiceUnknown",
                    "No activation entry exists for the requested name");
            }
        } else if (strcmp(member, "AddMatch") == 0 &&
                   strcmp(signature, "s") == 0) {
            char rule[AKAOS_DBUS_MAX_MATCH_RULE_LENGTH];
            if (!body_string(body, body_size, rule, sizeof(rule))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "AddMatch requires a bounded rule string");
            } else if (!add_match(unique, rule)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.LimitsExceeded",
                    "The D-Bus match-rule table is full or the rule is too long");
            } else {
                sent = send_method_return(client, serial, NULL, NULL, 0);
            }
            if (dbus_trace_enabled())
                fprintf(stderr,
                        "[dbus] AddMatch rule length=%zu reply sent=%d\n",
                        body_string(body, body_size, rule, sizeof(rule))
                            ? strlen(rule) : 0U,
                        sent);
        } else if (strcmp(member, "RemoveMatch") == 0 &&
                   strcmp(signature, "s") == 0) {
            char rule[AKAOS_DBUS_MAX_MATCH_RULE_LENGTH];
            if (!body_string(body, body_size, rule, sizeof(rule))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "RemoveMatch requires a bounded rule string");
            } else if (!remove_match(unique, rule)) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.MatchRuleNotFound",
                    "The requested match rule is not registered");
            } else {
                sent = send_method_return(client, serial, NULL, NULL, 0);
            }
        } else if (strcmp(member, "GetAllMatchRules") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            const char *empty[1] = {0};
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "GetAllMatchRules requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                /* Match rules are connection-local.  The public standard API
                 * still returns a correctly typed empty array for a new
                 * client; existing rule introspection remains bounded. */
                sent = send_string_array_return(client, serial, empty, 0U);
            }
        } else if (strcmp(member, "ListQueuedOwners") == 0 &&
                   strcmp(signature, "s") == 0) {
            char name[64];
            char owner[32];
            const char *owners[1];
            if (!body_string(body, body_size, name, sizeof(name))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "ListQueuedOwners requires a bus name");
            } else if (!name_owner(name, unique, owner, sizeof(owner))) {
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.NameHasNoOwner",
                    "The requested name has no owner");
            } else {
                owners[0] = owner;
                sent = send_string_array_return(client, serial, owners, 1U);
            }
        } else if (strcmp(member, "UpdateActivationEnvironment") == 0) {
            if (strcmp(signature, "a{ss}") != 0) {
                fprintf(stderr,
                        "[dbus] UpdateActivationEnvironment signature '%s'\n",
                        signature);
                sent = send_error_return(
                    client, serial, "org.freedesktop.DBus.Error.InvalidArgs",
                    "UpdateActivationEnvironment requires a string dictionary");
            } else {
                int valid = body_string_dictionary(body, body_size);
                fprintf(stderr,
                        "[dbus] UpdateActivationEnvironment bytes=%zu valid=%d\n",
                        body_size, valid);
                sent = valid
                    ? send_method_return(client, serial, NULL, NULL, 0)
                    : send_error_return(
                        client, serial,
                        "org.freedesktop.DBus.Error.InvalidArgs",
                        "invalid activation environment dictionary");
                if (!sent)
                    fprintf(stderr,
                            "[dbus] UpdateActivationEnvironment reply failed\n");
            }
        } else if (strcmp(member, "ReloadConfig") == 0 &&
                   signature[0] == '\0') {
            sent = send_method_return(client, serial, NULL, NULL, 0);
        } else if (strcmp(member, "ListNames") == 0) {
            sent = send_names_return(client, serial, unique);
        } else if (strcmp(member, "Ping") == 0) {
            sent = send_method_return(client, serial, NULL, NULL, 0);
        } else if (strcmp(member, "Introspect") == 0) {
            sent = send_string_return(client, serial, dbus_introspection_xml());
        } else {
            sent = send_error_return(
                client, serial, "org.freedesktop.DBus.Error.UnknownMethod",
                "The requested method is not implemented by the akaOS bus");
        }
        if (!sent) {
            return 0;
        }
    }
}

static struct dbus_client_context *register_binary_client(
    int fd, const struct ucred *peer_credentials) {
    if (!peer_credentials) return 0;
    pthread_mutex_lock(&dbus_state_lock);
    struct dbus_client_context *client = 0;
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_CLIENTS; ++index) {
        if (!dbus_clients[index].used) {
            client = &dbus_clients[index];
            break;
        }
    }
    if (client) {
        client->used = 1;
        client->fd = fd;
        client->peer_credentials = *peer_credentials;
        (void)snprintf(client->unique, sizeof(client->unique), ":1.%u",
                       dbus_next_unique++);
        (void)pthread_mutex_init(&client->write_lock, NULL);
    }
    pthread_mutex_unlock(&dbus_state_lock);
    return client;
}

static void *binary_client_worker(void *argument) {
    struct dbus_client_context *client = argument;
    if (!client) return 0;
    int result = handle_binary_client(client);
    if (dbus_trace_enabled())
        fprintf(stderr, "[dbus] standard client worker ended (result=%d)\n",
                result);
    fail_routes_for_client(client);
    release_connection_names(client->unique);
    release_connection_matches(client->unique);
    pthread_mutex_lock(&dbus_activation_lock);
    for (unsigned index = 0; index < AKAOS_DBUS_MAX_ACTIVATIONS; ++index) {
        struct dbus_activation_entry *entry = &dbus_activation_entries[index];
        if (entry->used && entry->started &&
            !find_client_for_name(entry->name)) {
            entry->started = 0;
        }
    }
    pthread_mutex_unlock(&dbus_activation_lock);
    close(client->fd);
    pthread_mutex_lock(&dbus_state_lock);
    client->used = 0;
    client->fd = -1;
    client->unique[0] = '\0';
    memset(&client->peer_credentials, 0, sizeof(client->peer_credentials));
    pthread_mutex_unlock(&dbus_state_lock);
    return 0;
}

static void daemon_backoff(void) {
    /* Keep the resident service out of the shell's input path while allowing
     * the kernel to schedule the login shell and other user processes. */
    for (unsigned count = 0; count < 4U; ++count) (void)sched_yield();
}

static void handle_client(int client, int unix_transport) {
    struct ucred peer_credentials;
    socklen_t peer_credentials_length = sizeof(peer_credentials);
    if (!unix_transport ||
        getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer_credentials,
                   &peer_credentials_length) != 0 ||
        peer_credentials_length != sizeof(peer_credentials)) {
        if (dbus_trace_enabled())
            fprintf(stderr, "[dbus] rejected client before peer credentials (errno=%d)\n",
                    errno);
        close(client);
        return;
    }
    if (dbus_trace_enabled())
        fprintf(stderr, "[dbus] UNIX client accepted\n");
    unsigned char first[AKAOS_DBUS_MAX_MESSAGE];
    ssize_t count = 0;
    if (!recv_bootstrap_line(client, first, sizeof(first), &count)) {
        close(client);
        return;
    }
    if (count > 0 && first[0] == 0) {
        int authenticated = authenticate_standard_client(
            client, first, count, peer_credentials.uid);
        if (dbus_trace_enabled())
            fprintf(stderr, "[dbus] standard client authentication %s\n",
                    authenticated ? "accepted" : "rejected");
        if (authenticated) {
            struct dbus_client_context *context = register_binary_client(
                client, &peer_credentials);
            if (!context || pthread_create(&context->thread, NULL,
                                           binary_client_worker, context) != 0) {
                if (dbus_trace_enabled())
                    fprintf(stderr, "[dbus] standard client worker creation failed\n");
                if (context) {
                    pthread_mutex_lock(&dbus_state_lock);
                    context->used = 0;
                    context->fd = -1;
                    memset(&context->peer_credentials, 0,
                           sizeof(context->peer_credentials));
                    pthread_mutex_unlock(&dbus_state_lock);
                }
                close(client);
                return;
            }
            (void)pthread_detach(context->thread);
            return;
        }
    } else if (count > 0) {
        /* The libc bootstrap sends this marker before waiting for the
         * greeting, avoiding a race with the standard auth handshake. */
        if (peer_credentials.uid == geteuid() && (size_t)count >= 6U &&
            memcmp(first, "NATIVE", 6U) == 0) {
            if (dbus_trace_enabled())
                fprintf(stderr, "[dbus] native bootstrap client accepted\n");
            (void)send_all(client, "AKAOS-DBUS 1\n");
            char message[AKAOS_DBUS_MAX_MESSAGE];
            if (recv_line(client, message, sizeof(message))) {
                if (strncmp(message, "PING", 4) == 0)
                    (void)send_all(client, "PONG\n");
                else if (strncmp(message, "HELLO", 5) == 0)
                    (void)send_all(client, "OK HELLO\n");
                else
                    (void)send_all(client, "OK METHOD-DISPATCH\n");
            }
        } else if (peer_credentials.uid == geteuid() &&
                   (size_t)count >= 4U && memcmp(first, "PING", 4U) == 0) {
            /* Compatibility for clients built against the original
             * greeting-first bootstrap. */
            (void)send_all(client, "AKAOS-DBUS 1\nPONG\n");
        }
    }
    close(client);
}

static void *unix_accept_worker(void *argument) {
    int listener = argument ? *(const int *)argument : -1;
    if (listener < 0) return NULL;
    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                daemon_backoff();
                continue;
            }
            return NULL;
        }
        handle_client(client, 1);
    }
}

int main(int argc, char **argv) {
    int session = 1;
    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0) {
            puts("akaOS D-Bus session bus 1.0 (native transport ABI 1)");
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[1], "--system") == 0) session = 0;
        else if (strcmp(argv[1], "--session") != 0) {
            usage();
            return 2;
        }
    }

    dbus_session_bus = session;
    initialize_dbus_guid(session);

    if (mkdir("/run", 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "dbus-daemon: mkdir /run failed (errno=%d)\n", errno);
    if (mkdir("/run/dbus", 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "dbus-daemon: mkdir /run/dbus failed (errno=%d)\n", errno);
    if (mkdir("/run/user", 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "dbus-daemon: mkdir /run/user failed (errno=%d)\n", errno);
    if (mkdir("/run/user/0", 0700) != 0 && errno != EEXIST)
        fprintf(stderr, "dbus-daemon: mkdir /run/user/0 failed (errno=%d)\n", errno);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        fprintf(stderr, "dbus-daemon: socket failed (errno=%d)\n", errno);
        return 1;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)(session ? AKAOS_DBUS_PORT : AKAOS_DBUS_PORT + 1U));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 8) < 0) {
        fprintf(stderr, "dbus-daemon: bind/listen failed (errno=%d)\n", errno);
        close(listener);
        return 1;
    }

    const char *unix_path = session ? AKAOS_DBUS_SESSION_ENDPOINT :
                                      AKAOS_DBUS_SYSTEM_ENDPOINT;
    /* Socket names are tracked by the kernel independently of VFS nodes.
     * Remove old marker files so the conventional paths are not mistaken
     * for ordinary files by userspace tools. */
    (void)unlink(unix_path);
    int unix_listener = create_unix_listener(unix_path);
    if (unix_listener < 0) {
        fprintf(stderr, "dbus-daemon: Unix socket bind/listen failed at %s (errno=%d)\n",
                unix_path, errno);
        close(listener);
        return 1;
    }
    publish_bus_address(session ? "/run/user/0/bus.address" :
                                   "/run/dbus/system-bus.address", unix_path);
    load_activation_registry();
    pthread_t supervisor;
    if (pthread_create(&supervisor, NULL, activation_supervisor, NULL) == 0)
        (void)pthread_detach(supervisor);

    pthread_t unix_accept_thread;
    if (pthread_create(&unix_accept_thread, NULL, unix_accept_worker,
                       &unix_listener) != 0) {
        fprintf(stderr, "dbus-daemon: unable to start Unix socket accept worker\n");
        close(unix_listener);
        close(listener);
        return 1;
    }
    (void)pthread_detach(unix_accept_thread);

    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                daemon_backoff();
                continue;
            }
            break;
        }
        handle_client(client, 0);
    }
    close(listener);
    close(unix_listener);
    return 0;
}
