#include <xscore/socket.h>
#include <xscore/file.h>
#include <xscore/net.h>
#include <xscore/smp.h>
#include <xscore/serial.h>
#include <xscore/thread.h>
#include <xscore/process.h>
#include <akaos/errno.h>
#include <stdint.h>
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);

static struct xscore_socket socket_pool[XSCORE_SOCKET_MAX];
/* Socket allocation/state is shared by AP-owned processes and network IRQ
 * demux. Keep lifecycle operations atomic while never holding this lock over
 * polling waits. */
static xscore_spinlock_t socket_pool_lock = XSCORE_SPINLOCK_INIT;
static uint16_t next_ephemeral_port = 49152;
static const uint8_t default_gateway_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint32_t default_guest_ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);

/* Ethernet, IPv4, and TCP headers share the fixed packet buffer used by the
 * tiny network backend.  Keep stream payloads below the packet ceiling so a
 * single write is never silently discarded by send_tcp_packet().  The
 * transport is a byte stream, so callers may submit arbitrarily sized D-Bus,
 * X11, or toolkit messages and they will be emitted as ordered segments. */
#define XSCORE_TCP_MAX_PAYLOAD 1400U

static uint16_t htons16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t htonl32(uint32_t v) {
    return ((v >> 24) & 0xff) |
           ((v >> 8)  & 0xff00) |
           ((v << 8)  & 0xff0000) |
           ((v << 24) & 0xff000000);
}

static uint16_t net_checksum(const void *buf, size_t len) {
    const uint16_t *data = buf;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)data;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *tcp_seg, size_t tcp_len) {
    struct {
        uint32_t src_ip;
        uint32_t dst_ip;
        uint8_t zero;
        uint8_t proto;
        uint16_t tcp_len;
    } __attribute__((packed)) pseudo;

    pseudo.src_ip = src_ip;
    pseudo.dst_ip = dst_ip;
    pseudo.zero = 0;
    pseudo.proto = 6;
    pseudo.tcp_len = htons16((uint16_t)tcp_len);

    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo) / 2; ++i) {
        sum += p[i];
    }

    const uint16_t *data = (const uint16_t *)tcp_seg;
    size_t len = tcp_len;
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)data;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void xscore_socket_initialize(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        memset(&socket_pool[i], 0, sizeof(struct xscore_socket));
        socket_pool[i].pending_socket_id = -1;
        socket_pool[i].peer_socket_id = -1;
        xscore_spinlock_init(&socket_pool[i].rx_consumer_lock);
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
}

int64_t xscore_socket_create(int domain, int type, int protocol) {
    if (domain != AKAOS_AF_INET && domain != AKAOS_AF_UNIX) return -AKAOS_EINVAL;
    if (type != AKAOS_SOCK_STREAM && type != AKAOS_SOCK_DGRAM) return -AKAOS_EINVAL;
    if (domain == AKAOS_AF_UNIX && type != AKAOS_SOCK_STREAM) return -AKAOS_EOPNOTSUPP;

    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (!socket_pool[i].in_use) {
            memset(&socket_pool[i], 0, sizeof(struct xscore_socket));
            socket_pool[i].in_use = 1;
            socket_pool[i].domain = domain;
            socket_pool[i].type = type;
            socket_pool[i].protocol = (type == AKAOS_SOCK_STREAM) ? AKAOS_IPPROTO_TCP : AKAOS_IPPROTO_UDP;
            if (protocol != 0) socket_pool[i].protocol = protocol;
            socket_pool[i].state = SOCKET_STATE_CLOSED;
            socket_pool[i].local_ip = default_guest_ip;
            socket_pool[i].pending_socket_id = -1;
            socket_pool[i].peer_socket_id = -1;
            xscore_spinlock_init(&socket_pool[i].rx_consumer_lock);
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return i;
        }
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return -AKAOS_EMFILE;
}

static size_t socket_path_length(const char *path) {
    if (!path) return 0;
    size_t length = 0;
    while (length + 1U < AKAOS_UNIX_PATH_MAX && path[length] != '\0') ++length;
    return length;
}

static int socket_path_equal(const char *left, const char *right) {
    if (!left || !right) return 0;
    size_t index = 0;
    while (index < AKAOS_UNIX_PATH_MAX && left[index] == right[index]) {
        if (left[index] == '\0') return 1;
        ++index;
    }
    return 0;
}

int64_t xscore_socket_bind_unix(int sock_id, const char *path) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !path || !*path ||
        socket_path_length(path) >= AKAOS_UNIX_PATH_MAX ||
        !socket_pool[sock_id].in_use ||
        socket_pool[sock_id].domain != AKAOS_AF_UNIX) {
        return -AKAOS_EINVAL;
    }
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (i != sock_id && socket_pool[i].in_use &&
            socket_pool[i].domain == AKAOS_AF_UNIX &&
            socket_pool[i].local_path[0] != '\0' &&
            socket_path_equal(socket_pool[i].local_path, path)) {
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return -AKAOS_EADDRINUSE;
        }
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    for (size_t i = 0; i < AKAOS_UNIX_PATH_MAX; ++i) sock->local_path[i] = '\0';
    for (size_t i = 0; path[i] != '\0' && i + 1U < AKAOS_UNIX_PATH_MAX; ++i)
        sock->local_path[i] = path[i];
    sock->state = SOCKET_STATE_BOUND;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_connect_unix(int sock_id, const char *path) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !path || !*path ||
        socket_path_length(path) >= AKAOS_UNIX_PATH_MAX ||
        !socket_pool[sock_id].in_use ||
        socket_pool[sock_id].domain != AKAOS_AF_UNIX ||
        socket_pool[sock_id].type != AKAOS_SOCK_STREAM) {
        return -AKAOS_EINVAL;
    }
    struct xscore_process *process = xscore_process_current();
    struct akaos_ucred connector_credentials = {0};
    int connector_credentials_valid = process != 0;
    if (process) {
        uint64_t pid = xscore_process_pid(process);
        if (pid > INT32_MAX) return -AKAOS_EOVERFLOW;
        connector_credentials.pid = (int32_t)pid;
        connector_credentials.uid = xscore_process_effective_uid(process);
        connector_credentials.gid = xscore_process_effective_gid(process);
    }

    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    int listener_id = -1;
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (socket_pool[i].in_use && socket_pool[i].domain == AKAOS_AF_UNIX &&
            socket_pool[i].type == AKAOS_SOCK_STREAM &&
            socket_pool[i].state == SOCKET_STATE_LISTEN &&
            socket_path_equal(socket_pool[i].local_path, path)) {
            listener_id = i;
            break;
        }
    }
    if (listener_id < 0) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_ENOENT;
    }
    if (socket_pool[listener_id].pending_socket_id >= 0) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EAGAIN;
    }
    int child_id = -1;
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (!socket_pool[i].in_use) { child_id = i; break; }
    }
    if (child_id < 0) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EMFILE;
    }
    struct xscore_socket *client = &socket_pool[sock_id];
    struct xscore_socket *child = &socket_pool[child_id];
    memset(child, 0, sizeof(*child));
    child->in_use = 1;
    child->domain = AKAOS_AF_UNIX;
    child->type = AKAOS_SOCK_STREAM;
    child->state = SOCKET_STATE_ESTABLISHED;
    child->pending_socket_id = -1;
    child->peer_socket_id = sock_id;
    xscore_spinlock_init(&child->rx_consumer_lock);
    child->peer_credentials = connector_credentials;
    child->peer_credentials_valid = connector_credentials_valid;
    client->peer_socket_id = child_id;
    client->state = SOCKET_STATE_ESTABLISHED;
    client->peer_credentials = socket_pool[listener_id].listener_credentials;
    client->peer_credentials_valid =
        socket_pool[listener_id].listener_credentials_valid;
    for (size_t i = 0; path[i] != '\0' && i + 1U < AKAOS_UNIX_PATH_MAX; ++i) {
        client->remote_path[i] = path[i];
        child->local_path[i] = path[i];
        child->remote_path[i] = client->local_path[i];
    }
    socket_pool[listener_id].pending_socket_id = child_id;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

static void send_tcp_packet(struct xscore_socket *sock, uint8_t flags, const void *payload, size_t payload_len) {
    if (!sock) return;

    static uint8_t buffer[1536];
    size_t total_len = sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_tcp_hdr) + payload_len;
    if (total_len > sizeof(buffer)) return;

    memset(buffer, 0, total_len);

    struct akaos_eth_hdr *eth = (struct akaos_eth_hdr *)buffer;
    struct akaos_ip_hdr *ip = (struct akaos_ip_hdr *)(buffer + sizeof(struct akaos_eth_hdr));
    struct akaos_tcp_hdr *tcp = (struct akaos_tcp_hdr *)(buffer + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));

    if (sock->remote_ip == ((127) | (0 << 8) | (0 << 16) | (1 << 24))) {
        memset(eth->dst, 0x00, 6);
    } else {
        memcpy(eth->dst, default_gateway_mac, 6);
    }
    memcpy(eth->src, default_gateway_mac, 6);
    eth->type = htons16(AKAOS_ETH_TYPE_IP);

    ip->v_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons16((uint16_t)(sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_tcp_hdr) + payload_len));
    ip->id = htons16((uint16_t)sock->seq);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = AKAOS_IPPROTO_TCP;
    ip->src_ip = sock->local_ip;
    ip->dst_ip = sock->remote_ip;
    ip->checksum = 0;
    ip->checksum = net_checksum(ip, sizeof(struct akaos_ip_hdr));

    tcp->src_port = htons16(sock->local_port);
    tcp->dst_port = htons16(sock->remote_port);
    tcp->seq = htonl32(sock->seq);
    tcp->ack = htonl32(sock->ack);
    tcp->data_offset_reserved = (uint8_t)((sizeof(struct akaos_tcp_hdr) / 4) << 4);
    tcp->flags = flags;
    tcp->window_size = htons16(8192);
    tcp->checksum = 0;

    if (payload && payload_len > 0) {
        memcpy(buffer + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_tcp_hdr),
               payload, payload_len);
    }

    tcp->checksum = tcp_checksum(ip->src_ip, ip->dst_ip, tcp, sizeof(struct akaos_tcp_hdr) + payload_len);

    xscore_net_transmit(buffer, total_len);
}

int64_t xscore_socket_connect(int sock_id, uint32_t ip, uint16_t port) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    sock->remote_ip = ip;
    sock->remote_port = port;
    if (ip == ((127) | (0 << 8) | (0 << 16) | (1 << 24))) {
        sock->local_ip = ip;
    }

    if (sock->local_port == 0) {
        sock->local_port = next_ephemeral_port++;
        if (next_ephemeral_port > 65000) next_ephemeral_port = 49152;
    }

    if (sock->type == AKAOS_SOCK_DGRAM) {
        sock->state = SOCKET_STATE_BOUND;
        return 0;
    }

    /* Publish the consumed SYN sequence before receive dispatch can run. */
    uintptr_t tcp_flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    sock->seq = 1000;
    sock->tcp_acked_seq = sock->seq;
    sock->ack = 0;
    sock->state = SOCKET_STATE_SYN_SENT;

    send_tcp_packet(sock, AKAOS_TCP_FLAG_SYN, 0, 0);
    ++sock->seq;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, tcp_flags);

    /* Poll network for SYN-ACK */
    for (int spin = 0; spin < 200; ++spin) {
        if (xscore_virtio_net_present()) {
            xscore_virtio_net_poll_rx();
        }
        xscore_net_poll_loopback();
        if (sock->state == SOCKET_STATE_ESTABLISHED) {
            return 0;
        }
        /* Let a loopback listener run while the SYN is being completed. */
        xscore_thread_yield();
        for (volatile int d = 0; d < 50000; ++d) {}
    }

    return -AKAOS_ETIMEDOUT;
}

int64_t xscore_socket_bind(int sock_id, uint32_t ip, uint16_t port) {
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    sock->local_ip = (ip == 0) ? default_guest_ip : ip;
    sock->local_port = port;
    sock->state = SOCKET_STATE_BOUND;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_listen(int sock_id, int backlog) {
    (void)backlog;
    struct xscore_process *process = xscore_process_current();
    struct akaos_ucred listener_credentials = {0};
    int listener_credentials_valid = process != 0;
    if (process) {
        uint64_t pid = xscore_process_pid(process);
        if (pid > INT32_MAX) return -AKAOS_EOVERFLOW;
        listener_credentials.pid = (int32_t)pid;
        listener_credentials.uid = xscore_process_effective_uid(process);
        listener_credentials.gid = xscore_process_effective_gid(process);
    }
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->type != AKAOS_SOCK_STREAM) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EOPNOTSUPP;
    }
    sock->state = SOCKET_STATE_LISTEN;
    sock->listener_credentials = listener_credentials;
    sock->listener_credentials_valid = listener_credentials_valid;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_get_peer_credentials(
    int sock_id, struct akaos_ucred *credentials) {
    if (!credentials) return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    const struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->domain != AKAOS_AF_UNIX) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EOPNOTSUPP;
    }
    if (!sock->peer_credentials_valid) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_ENOTCONN;
    }
    *credentials = sock->peer_credentials;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_get_name(int sock_id, int peer, int *domain,
                               uint32_t *ip, uint16_t *port,
                               char *path, size_t path_capacity,
                               size_t *path_length) {
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    const struct xscore_socket *sock = &socket_pool[sock_id];
    if (domain) *domain = sock->domain;
    if (sock->domain == AKAOS_AF_UNIX) {
        if (peer && sock->peer_socket_id < 0) {
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return -AKAOS_ENOTCONN;
        }
        const char *source = peer ? sock->remote_path : sock->local_path;
        size_t length = 0U;
        while (source[length]) ++length;
        if (path && path_capacity) {
            size_t copied = length < path_capacity - 1U
                ? length : path_capacity - 1U;
            for (size_t index = 0U; index < copied; ++index)
                path[index] = source[index];
            path[copied] = '\0';
        }
        if (path_length) *path_length = length;
        if (ip) *ip = 0U;
        if (port) *port = 0U;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return 0;
    }
    if (sock->domain != AKAOS_AF_INET) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EAFNOSUPPORT;
    }
    if (peer && sock->remote_port == 0U) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_ENOTCONN;
    }
    if (ip) *ip = peer ? sock->remote_ip : sock->local_ip;
    if (port) *port = peer ? sock->remote_port : sock->local_port;
    if (path && path_capacity) path[0] = '\0';
    if (path_length) *path_length = 0U;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_get_option(int sock_id, int level, int option,
                                 uint32_t *value) {
    if (!value) return -AKAOS_EFAULT;
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    const struct xscore_socket *sock = &socket_pool[sock_id];
    int64_t result = 0;
    if (level == AKAOS_SOL_SOCKET) {
        switch (option) {
            case AKAOS_SO_TYPE: *value = (uint32_t)sock->type; break;
            case AKAOS_SO_DOMAIN: *value = (uint32_t)sock->domain; break;
            case AKAOS_SO_ERROR: *value = 0U; break;
            case AKAOS_SO_SNDBUF:
            case AKAOS_SO_RCVBUF: *value = XSCORE_SOCKET_BUF_SIZE; break;
            case AKAOS_SO_KEEPALIVE:
            case AKAOS_SO_BROADCAST:
            case AKAOS_SO_REUSEADDR:
            case AKAOS_SO_REUSEPORT: *value = 0U; break;
            default: result = -AKAOS_ENOPROTOOPT; break;
        }
    } else if (level == AKAOS_IPPROTO_TCP && option == AKAOS_TCP_NODELAY &&
               sock->domain == AKAOS_AF_INET &&
               sock->type == AKAOS_SOCK_STREAM) {
        /* The native TCP sender emits each write immediately. */
        *value = 1U;
    } else {
        result = -AKAOS_ENOPROTOOPT;
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return result;
}

int64_t xscore_socket_set_option(int sock_id, int level, int option,
                                 uint32_t value) {
    (void)value;
    uint32_t current = 0U;
    int64_t result = xscore_socket_get_option(sock_id, level, option, &current);
    if (result < 0) return result;
    if ((level == AKAOS_IPPROTO_TCP && option == AKAOS_TCP_NODELAY) ||
        (level == AKAOS_SOL_SOCKET &&
         (option == AKAOS_SO_KEEPALIVE || option == AKAOS_SO_BROADCAST ||
          option == AKAOS_SO_REUSEADDR || option == AKAOS_SO_REUSEPORT)))
        return 0;
    /* Reject options whose behavior the current transport does not implement. */
    return -AKAOS_EOPNOTSUPP;
}

int64_t xscore_socket_shutdown(int sock_id, int how) {
    if (how < 0 || how > 2) return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->state != SOCKET_STATE_ESTABLISHED &&
        sock->state != SOCKET_STATE_CLOSE_WAIT) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_ENOTCONN;
    }
    if (how == 0 || how == 2) sock->read_shutdown = 1U;
    if ((how == 1 || how == 2) && !sock->write_shutdown) {
        sock->write_shutdown = 1U;
        if (sock->domain == AKAOS_AF_UNIX && sock->peer_socket_id >= 0 &&
            sock->peer_socket_id < XSCORE_SOCKET_MAX) {
            struct xscore_socket *peer = &socket_pool[sock->peer_socket_id];
            if (peer->in_use && peer->peer_socket_id == sock_id)
                peer->peer_write_shutdown = 1U;
        } else if (sock->domain == AKAOS_AF_INET &&
                   sock->type == AKAOS_SOCK_STREAM) {
            send_tcp_packet(sock, AKAOS_TCP_FLAG_FIN | AKAOS_TCP_FLAG_ACK,
                            0, 0U);
            ++sock->seq;
        }
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int64_t xscore_socket_accept(int sock_id, uint32_t *remote_ip, uint16_t *remote_port) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->state != SOCKET_STATE_LISTEN) return -AKAOS_EINVAL;

    for (int spin = 0; spin < 500; ++spin) {
        if (xscore_virtio_net_present()) {
            xscore_virtio_net_poll_rx();
        }
        xscore_net_poll_loopback();
        uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
        int child_id = -1;
        if (sock_id >= 0 && sock_id < XSCORE_SOCKET_MAX &&
            socket_pool[sock_id].in_use &&
            socket_pool[sock_id].state == SOCKET_STATE_LISTEN &&
            socket_pool[sock_id].pending_socket_id >= 0 &&
            socket_pool[socket_pool[sock_id].pending_socket_id].state ==
                SOCKET_STATE_ESTABLISHED) {
            sock = &socket_pool[sock_id];
            child_id = sock->pending_socket_id;
            sock->pending_socket_id = -1;
        }
        if (child_id >= 0 && child_id < XSCORE_SOCKET_MAX &&
            socket_pool[child_id].in_use) {
            if (remote_ip) *remote_ip = socket_pool[child_id].remote_ip;
            if (remote_port) *remote_port = socket_pool[child_id].remote_port;
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return child_id;
        }
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        /* A blocking accept must not monopolize the single-core guest while
         * another process is trying to connect to this listener. */
        xscore_thread_yield();
        for (volatile int d = 0; d < 30000; ++d) {}
    }

    return -AKAOS_EAGAIN;
}

int64_t xscore_socket_sendto(int sock_id, const void *buf, size_t len, uint32_t dest_ip, uint16_t dest_port) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use || !buf) {
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->write_shutdown) return -AKAOS_EPIPE;

    if (sock->local_port == 0) {
        sock->local_port = next_ephemeral_port++;
        if (next_ephemeral_port > 65000) next_ephemeral_port = 49152;
    }

    if (sock->type == AKAOS_SOCK_DGRAM) {
        static uint8_t buffer[1536];
        size_t total_len = sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_udp_hdr) + len;
        if (total_len > sizeof(buffer)) return -AKAOS_EMSGSIZE;

        memset(buffer, 0, total_len);

        struct akaos_eth_hdr *eth = (struct akaos_eth_hdr *)buffer;
        struct akaos_ip_hdr *ip = (struct akaos_ip_hdr *)(buffer + sizeof(struct akaos_eth_hdr));
        struct akaos_udp_hdr *udp = (struct akaos_udp_hdr *)(buffer + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));

        memcpy(eth->dst, default_gateway_mac, 6);
        memcpy(eth->src, default_gateway_mac, 6);
        eth->type = htons16(AKAOS_ETH_TYPE_IP);

        ip->v_ihl = 0x45;
        ip->tos = 0;
        ip->total_len = htons16((uint16_t)(sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_udp_hdr) + len));
        ip->id = htons16(1);
        ip->flags_frag = 0;
        ip->ttl = 64;
        ip->proto = AKAOS_IPPROTO_UDP;
        ip->src_ip = sock->local_ip;
        ip->dst_ip = dest_ip;
        ip->checksum = 0;
        ip->checksum = net_checksum(ip, sizeof(struct akaos_ip_hdr));

        udp->src_port = htons16(sock->local_port);
        udp->dst_port = htons16(dest_port);
        udp->length = htons16((uint16_t)(sizeof(struct akaos_udp_hdr) + len));
        udp->checksum = 0;

        memcpy(buffer + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_udp_hdr),
               buf, len);

        xscore_net_transmit(buffer, total_len);
        return (int64_t)len;
    }

    return xscore_socket_send(sock_id, buf, len);
}

int64_t xscore_socket_send(int sock_id, const void *buf, size_t len) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use || !buf) {
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->write_shutdown) return -AKAOS_EPIPE;

    if (sock->domain == AKAOS_AF_UNIX) {
        if (sock->state != SOCKET_STATE_ESTABLISHED ||
            sock->peer_socket_id < 0 || sock->peer_socket_id >= XSCORE_SOCKET_MAX)
            return -AKAOS_ENOTCONN;
        uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
        struct xscore_socket *peer = &socket_pool[sock->peer_socket_id];
        if (!peer->in_use || peer->read_shutdown ||
            peer->state == SOCKET_STATE_CLOSED ||
            peer->state == SOCKET_STATE_CLOSE_WAIT) {
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return -AKAOS_EPIPE;
        }
        size_t available = XSCORE_SOCKET_BUF_SIZE - peer->rx_count;
        size_t to_copy = len < available ? len : available;
        if (to_copy == 0U && len > 0U) {
            /* The buffer of the peer is full.  A socket that is not in non
             * blocking mode waits for the peer to read, and one that is
             * answers with EAGAIN. */
            xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
            return -AKAOS_EAGAIN;
        }
        for (size_t i = 0; i < to_copy; ++i) {
            peer->rx_buf[peer->rx_head] = ((const uint8_t *)buf)[i];
            peer->rx_head = (peer->rx_head + 1U) % XSCORE_SOCKET_BUF_SIZE;
        }
        peer->rx_count += (uint32_t)to_copy;
        peer->rx_write_position += (uint64_t)to_copy;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return (int64_t)to_copy;
    }

    if (sock->type == AKAOS_SOCK_DGRAM) {
        return xscore_socket_sendto(sock_id, buf, len, sock->remote_ip, sock->remote_port);
    }

    if (sock->state != SOCKET_STATE_ESTABLISHED) {
        return -AKAOS_ENOTCONN;
    }

    const uint8_t *payload = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        size_t remaining = len - sent;
        size_t chunk = remaining > XSCORE_TCP_MAX_PAYLOAD
            ? XSCORE_TCP_MAX_PAYLOAD : remaining;
        uintptr_t tcp_flags = xscore_spin_lock_irqsave(&socket_pool_lock);
        if (!sock->in_use || sock->state != SOCKET_STATE_ESTABLISHED ||
            sock->write_shutdown) {
            xscore_spin_unlock_irqrestore(&socket_pool_lock, tcp_flags);
            return sent ? (int64_t)sent : -AKAOS_ENOTCONN;
        }
        send_tcp_packet(sock, AKAOS_TCP_FLAG_ACK | AKAOS_TCP_FLAG_PSH,
                        payload + sent, chunk);
        sock->seq += (uint32_t)chunk;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, tcp_flags);
        sent += chunk;
    }
    return (int64_t)sent;
}

static int socket_rights_find_position(const struct xscore_socket *sock,
                                       uint64_t position) {
    for (uint32_t index = 0U; index < sock->rights_record_count; ++index)
        if (sock->rights_records[index].stream_position == position)
            return (int)index;
    return -1;
}

static int socket_rights_next_position(const struct xscore_socket *sock,
                                       uint64_t position,
                                       uint64_t *next_position) {
    int found = 0;
    uint64_t nearest = UINT64_MAX;
    for (uint32_t index = 0U; index < sock->rights_record_count; ++index) {
        uint64_t candidate = sock->rights_records[index].stream_position;
        if (candidate > position && candidate < nearest) {
            nearest = candidate;
            found = 1;
        }
    }
    if (found && next_position) *next_position = nearest;
    return found;
}

static size_t socket_rights_remove_locked(
    struct xscore_socket *sock, uint32_t index,
    struct xscore_file **released_files, size_t capacity) {
    if (!sock || index >= sock->rights_record_count) return 0U;
    struct xscore_unix_rights_record *record = &sock->rights_records[index];
    size_t count = record->file_count;
    for (size_t file_index = 0U; file_index < count; ++file_index) {
        if (released_files && file_index < capacity)
            released_files[file_index] = record->files[file_index];
    }
    for (uint32_t move = index; move + 1U < sock->rights_record_count; ++move)
        sock->rights_records[move] = sock->rights_records[move + 1U];
    --sock->rights_record_count;
    memset(&sock->rights_records[sock->rights_record_count], 0,
           sizeof(sock->rights_records[0]));
    return count;
}

int64_t xscore_socket_sendmsg(int sock_id, const void *buffer, size_t length,
                              struct xscore_file *const *files,
                              size_t file_count) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use || (length != 0U && !buffer) ||
        file_count > XSCORE_UNIX_RIGHTS_MAX ||
        (file_count != 0U && !files) ||
        length > XSCORE_SOCKET_BUF_SIZE)
        return -AKAOS_EINVAL;
    if (file_count == 0U) {
        if (length == 0U) return 0;
        return xscore_socket_send(sock_id, buffer, length);
    }
    for (size_t index = 0U; index < file_count; ++index)
        if (!files[index]) return -AKAOS_EBADF;

    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (!sock->in_use || sock->domain != AKAOS_AF_UNIX ||
        sock->type != AKAOS_SOCK_STREAM) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EOPNOTSUPP;
    }
    if (sock->state != SOCKET_STATE_ESTABLISHED || sock->peer_socket_id < 0 ||
        sock->peer_socket_id >= XSCORE_SOCKET_MAX) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_ENOTCONN;
    }
    struct xscore_socket *peer = &socket_pool[sock->peer_socket_id];
    if (sock->write_shutdown || !peer->in_use || peer->read_shutdown ||
        peer->state == SOCKET_STATE_CLOSED ||
        peer->state == SOCKET_STATE_CLOSE_WAIT) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EPIPE;
    }
    int existing_record = socket_rights_find_position(
        peer, peer->rx_write_position);
    if ((existing_record < 0 &&
         peer->rights_record_count >= XSCORE_UNIX_RIGHTS_QUEUE) ||
        (existing_record >= 0 &&
         file_count > XSCORE_UNIX_RIGHTS_MAX -
             peer->rights_records[existing_record].file_count) ||
        length > XSCORE_SOCKET_BUF_SIZE - peer->rx_count) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return -AKAOS_EAGAIN;
    }

    struct xscore_unix_rights_record *record;
    if (existing_record >= 0) {
        record = &peer->rights_records[existing_record];
    } else {
        record = &peer->rights_records[peer->rights_record_count++];
        memset(record, 0, sizeof(*record));
        record->stream_position = peer->rx_write_position;
    }
    for (size_t index = 0U; index < file_count; ++index)
        record->files[record->file_count + index] = files[index];
    record->file_count += (uint32_t)file_count;
    const uint8_t *bytes = buffer;
    for (size_t index = 0U; index < length; ++index) {
        peer->rx_buf[peer->rx_head] = bytes[index];
        peer->rx_head = (peer->rx_head + 1U) % XSCORE_SOCKET_BUF_SIZE;
    }
    peer->rx_count += (uint32_t)length;
    peer->rx_write_position += (uint64_t)length;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return (int64_t)length;
}

int64_t xscore_socket_recvmsg(int sock_id, struct xscore_fd_table *table,
                              void *buffer, size_t capacity,
                              int *received_fds, size_t fd_capacity,
                              size_t *data_length, size_t *fd_count,
                              int *message_flags) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !table ||
        !data_length || !fd_count || !message_flags ||
        (capacity != 0U && !buffer) ||
        (fd_capacity != 0U && !received_fds) ||
        fd_capacity > XSCORE_UNIX_RIGHTS_MAX ||
        !socket_pool[sock_id].in_use)
        return -AKAOS_EINVAL;

    struct xscore_socket *sock = &socket_pool[sock_id];
    uintptr_t consumer_flags =
        xscore_spin_lock_irqsave(&sock->rx_consumer_lock);
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (!sock->in_use || sock->domain != AKAOS_AF_UNIX ||
        sock->type != AKAOS_SOCK_STREAM) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                      consumer_flags);
        return -AKAOS_EOPNOTSUPP;
    }

    int rights_index = socket_rights_find_position(sock,
                                                    sock->rx_read_position);
    if (sock->read_shutdown) {
        *data_length = 0U;
        *fd_count = 0U;
        *message_flags = 0;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock, consumer_flags);
        return 0;
    }
    if (sock->rx_count == 0U && rights_index < 0) {
        int eof = sock->peer_write_shutdown ||
                  sock->state == SOCKET_STATE_CLOSE_WAIT ||
                  sock->state == SOCKET_STATE_CLOSED;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                      consumer_flags);
        return eof ? 0 : -AKAOS_EAGAIN;
    }

    size_t to_copy = capacity < sock->rx_count ? capacity : sock->rx_count;
    int rights_boundary_limited = 0;
    uint64_t next_rights_position = 0U;
    if (socket_rights_next_position(sock, sock->rx_read_position,
                                    &next_rights_position)) {
        uint64_t distance = next_rights_position - sock->rx_read_position;
        if (distance < (uint64_t)to_copy) {
            to_copy = (size_t)distance;
            rights_boundary_limited = 1;
        }
    }
    struct xscore_file *queued_files[XSCORE_UNIX_RIGHTS_MAX] = {0};
    size_t queued_count = 0U;
    if (rights_index >= 0) {
        const struct xscore_unix_rights_record *record =
            &sock->rights_records[rights_index];
        queued_count = record->file_count;
        for (size_t index = 0U; index < queued_count; ++index)
            queued_files[index] = record->files[index];
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);

    size_t delivered_count = queued_count < fd_capacity
        ? queued_count : fd_capacity;
    if (delivered_count != 0U) {
        int64_t installed = xscore_fd_transfer_install_many(
            table, queued_files, delivered_count, received_fds);
        if (installed < 0) {
            xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                          consumer_flags);
            return installed;
        }
    }

    flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (!sock->in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        for (size_t index = 0U; index < delivered_count; ++index)
            (void)xscore_fd_close(table, received_fds[index]);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                      consumer_flags);
        return -AKAOS_EBADF;
    }
    for (size_t index = 0U; index < to_copy; ++index) {
        ((uint8_t *)buffer)[index] = sock->rx_buf[sock->rx_tail];
        sock->rx_tail = (sock->rx_tail + 1U) % XSCORE_SOCKET_BUF_SIZE;
    }
    sock->rx_count -= (uint32_t)to_copy;
    sock->rx_read_position += (uint64_t)to_copy;
    if (rights_index >= 0) {
        (void)socket_rights_remove_locked(
            sock, (uint32_t)rights_index, queued_files,
            XSCORE_UNIX_RIGHTS_MAX);
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);

    for (size_t index = 0U; index < queued_count; ++index)
        xscore_fd_transfer_release(queued_files[index]);
    xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock, consumer_flags);
    *data_length = to_copy;
    *fd_count = delivered_count;
    *message_flags = queued_count > delivered_count
        ? (int)AKAOS_MSG_CTRUNC : 0;
    /* Report a truncated transfer when the caller's buffer could not hold
     * the pending bytes.  A descriptor boundary is a different condition and
     * must not be reported as truncation. */
    if (!rights_boundary_limited && to_copy < capacity && sock->rx_count > 0U) {
        *message_flags |= (int)AKAOS_MSG_TRUNC;
    }
    return (int64_t)to_copy;
}

int64_t xscore_socket_recvfrom(int sock_id, void *buf, size_t len, uint32_t *src_ip, uint16_t *src_port) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use || !buf) {
        return -AKAOS_EBADF;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];

    if (sock->rx_count == 0 && sock->domain != AKAOS_AF_UNIX) {
        for (int retry = 0; retry < 50 && sock->rx_count == 0; ++retry) {
            if (xscore_virtio_net_present()) {
                xscore_virtio_net_poll_rx();
            }
            xscore_net_poll_loopback();
            if (sock->rx_count > 0) break;
            xscore_thread_yield();
            for (volatile int d = 0; d < 20000; ++d) {}
        }
    }

    uintptr_t consumer_flags =
        xscore_spin_lock_irqsave(&sock->rx_consumer_lock);
    uintptr_t pool_flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (!sock->in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, pool_flags);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                      consumer_flags);
        return -AKAOS_EBADF;
    }

    int rights_here = sock->domain == AKAOS_AF_UNIX
        ? socket_rights_find_position(sock, sock->rx_read_position) : -1;
    if (sock->read_shutdown) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, pool_flags);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock, consumer_flags);
        return 0;
    }
    if (sock->rx_count == 0U) {
        int eof = sock->type == AKAOS_SOCK_STREAM &&
            (sock->peer_write_shutdown ||
             sock->state == SOCKET_STATE_CLOSE_WAIT ||
             sock->state == SOCKET_STATE_CLOSED);
        struct xscore_file *discarded[XSCORE_UNIX_RIGHTS_MAX] = {0};
        size_t discarded_count = 0U;
        if (rights_here >= 0 && len != 0U) {
            discarded_count = socket_rights_remove_locked(
                sock, (uint32_t)rights_here, discarded,
                XSCORE_UNIX_RIGHTS_MAX);
        }
        xscore_spin_unlock_irqrestore(&socket_pool_lock, pool_flags);
        for (size_t index = 0U; index < discarded_count; ++index)
            xscore_fd_transfer_release(discarded[index]);
        xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock,
                                      consumer_flags);
        if (rights_here >= 0 && len != 0U) return 0;
        return eof ? 0 : -AKAOS_EAGAIN;
    }

    size_t to_copy = (len < sock->rx_count) ? len : sock->rx_count;
    for (size_t index = 0U; index < to_copy; ++index) {
        ((uint8_t *)buf)[index] = sock->rx_buf[sock->rx_tail];
        sock->rx_tail = (sock->rx_tail + 1U) % XSCORE_SOCKET_BUF_SIZE;
    }
    sock->rx_count -= (uint32_t)to_copy;
    sock->rx_read_position += (uint64_t)to_copy;

    struct xscore_file *discarded[XSCORE_UNIX_RIGHTS_QUEUE *
                                  XSCORE_UNIX_RIGHTS_MAX];
    size_t discarded_count = 0U;
    if (sock->domain == AKAOS_AF_UNIX && to_copy != 0U) {
        while (sock->rights_record_count != 0U) {
            uint32_t record_index = 0U;
            while (record_index < sock->rights_record_count &&
                   sock->rights_records[record_index].stream_position >
                       sock->rx_read_position)
                ++record_index;
            if (record_index == sock->rights_record_count) break;
            discarded_count += socket_rights_remove_locked(
                sock, record_index, discarded + discarded_count,
                (XSCORE_UNIX_RIGHTS_QUEUE * XSCORE_UNIX_RIGHTS_MAX) -
                    discarded_count);
        }
    }
    uint32_t remote_ip = sock->remote_ip;
    uint16_t remote_port = sock->remote_port;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, pool_flags);
    for (size_t index = 0U; index < discarded_count; ++index)
        xscore_fd_transfer_release(discarded[index]);
    xscore_spin_unlock_irqrestore(&sock->rx_consumer_lock, consumer_flags);

    if (src_ip) *src_ip = remote_ip;
    if (src_port) *src_port = remote_port;
    return (int64_t)to_copy;
}

int64_t xscore_socket_receive(int sock_id, void *buf, size_t len) {
    return xscore_socket_recvfrom(sock_id, buf, len, 0, 0);
}

int xscore_socket_poll_readable(int sock_id) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) return 0;
    if (xscore_virtio_net_present()) {
        xscore_virtio_net_poll_rx();
    }
    xscore_net_poll_loopback();
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    struct xscore_socket *sock = &socket_pool[sock_id];
    if (sock->state == SOCKET_STATE_LISTEN) {
        int readable = sock->pending_socket_id >= 0 &&
            socket_pool[sock->pending_socket_id].state == SOCKET_STATE_ESTABLISHED;
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return readable;
    }
    if (sock->rx_count > 0 ||
        (sock->domain == AKAOS_AF_UNIX &&
         socket_rights_find_position(sock, sock->rx_read_position) >= 0)) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return 1;
    }
    if (sock->read_shutdown || (sock->type == AKAOS_SOCK_STREAM &&
        (sock->peer_write_shutdown || sock->state == SOCKET_STATE_CLOSE_WAIT ||
         sock->state == SOCKET_STATE_CLOSED))) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return 1; /* EOF triggers POLLIN */
    }
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return 0;
}

int xscore_socket_poll_writable(int sock_id) {
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return 0;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    int writable = sock->state == SOCKET_STATE_ESTABLISHED ||
                   sock->type == AKAOS_SOCK_DGRAM;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return writable;
}

size_t xscore_socket_close_collect(int sock_id,
                                   struct xscore_file **released_files,
                                   size_t capacity) {
    if (capacity < XSCORE_UNIX_RIGHTS_QUEUE * XSCORE_UNIX_RIGHTS_MAX ||
        !released_files) return 0U;
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX || !socket_pool[sock_id].in_use) {
        xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
        return 0U;
    }
    struct xscore_socket *sock = &socket_pool[sock_id];
    size_t released_count = 0U;
    while (sock->rights_record_count != 0U) {
        released_count += socket_rights_remove_locked(
            sock, 0U, released_files + released_count,
            capacity - released_count);
    }
    if (sock->domain == AKAOS_AF_UNIX && sock->peer_socket_id >= 0 &&
        sock->peer_socket_id < XSCORE_SOCKET_MAX) {
        struct xscore_socket *peer = &socket_pool[sock->peer_socket_id];
        if (peer->in_use && peer->peer_socket_id == sock_id) {
            peer->state = SOCKET_STATE_CLOSE_WAIT;
            peer->peer_socket_id = -1;
        }
    }
    if (sock->domain == AKAOS_AF_INET && sock->type == AKAOS_SOCK_STREAM &&
        sock->state == SOCKET_STATE_ESTABLISHED) {
        send_tcp_packet(sock, AKAOS_TCP_FLAG_FIN | AKAOS_TCP_FLAG_ACK, 0, 0);
        sock->state = SOCKET_STATE_CLOSED;
    }
    sock->in_use = 0;
    sock->state = SOCKET_STATE_CLOSED;
    sock->rx_count = 0;
    sock->rx_read_position = 0U;
    sock->rx_write_position = 0U;
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
    return released_count;
}

void xscore_socket_close(int sock_id) {
    struct xscore_file *released_files[XSCORE_UNIX_RIGHTS_QUEUE *
                                       XSCORE_UNIX_RIGHTS_MAX];
    size_t released_count = xscore_socket_close_collect(
        sock_id, released_files,
        XSCORE_UNIX_RIGHTS_QUEUE * XSCORE_UNIX_RIGHTS_MAX);
    for (size_t index = 0U; index < released_count; ++index)
        xscore_fd_transfer_release(released_files[index]);
}

int xscore_socket_domain(int sock_id) {
    if (sock_id < 0 || sock_id >= XSCORE_SOCKET_MAX ||
        !socket_pool[sock_id].in_use) return -1;
    return socket_pool[sock_id].domain;
}

void xscore_socket_demux_udp(const void *packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip) {
    (void)dst_ip;
    if (!packet || len < sizeof(struct akaos_udp_hdr)) return;
    const struct akaos_udp_hdr *udp = (const struct akaos_udp_hdr *)packet;
    uint16_t dst_port = htons16(udp->dst_port);
    uint16_t src_port = htons16(udp->src_port);
    uint16_t udp_len = htons16(udp->length);
    if (udp_len > len) return;
    uint32_t payload_len = (uint32_t)(udp_len - sizeof(struct akaos_udp_hdr));
    const uint8_t *payload = (const uint8_t *)packet + sizeof(struct akaos_udp_hdr);

    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (socket_pool[i].in_use && socket_pool[i].type == AKAOS_SOCK_DGRAM &&
            socket_pool[i].local_port == dst_port) {
            struct xscore_socket *s = &socket_pool[i];
            s->remote_ip = src_ip;
            s->remote_port = src_port;
            for (uint32_t b = 0; b < payload_len; ++b) {
                if (s->rx_count < XSCORE_SOCKET_BUF_SIZE) {
                    s->rx_buf[s->rx_head] = payload[b];
                    s->rx_head = (s->rx_head + 1) % XSCORE_SOCKET_BUF_SIZE;
                    s->rx_count++;
                }
            }
            return;
        }
    }
}

/* The small TCP backend accepts only the next in-order segment.  Keep all
 * endpoint and state checks ahead of receive-buffer or lifecycle changes. */
static void socket_demux_tcp_locked(const void *packet, uint32_t len,
                                    uint32_t src_ip, uint32_t dst_ip) {
    if (!packet || len < sizeof(struct akaos_tcp_hdr)) return;
    const struct akaos_tcp_hdr *tcp = packet;
    uint16_t dst_port = htons16(tcp->dst_port);
    uint16_t src_port = htons16(tcp->src_port);
    uint32_t seq = htonl32(tcp->seq);
    uint32_t ack = htonl32(tcp->ack);
    uint8_t flags = tcp->flags;
    uint32_t hlen = (tcp->data_offset_reserved >> 4) * 4U;
    if (hlen < sizeof(*tcp) || hlen > len) return;
    uint32_t payload_len = len - hlen;
    const uint8_t *payload = (const uint8_t *)packet + hlen;

    /* Connections take precedence over listeners, including retransmits. */
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        struct xscore_socket *s = &socket_pool[i];
        if (!s->in_use || s->domain != AKAOS_AF_INET ||
            s->type != AKAOS_SOCK_STREAM || s->protocol != AKAOS_IPPROTO_TCP ||
            s->state == SOCKET_STATE_LISTEN ||
            s->local_ip != dst_ip || s->remote_ip != src_ip ||
            s->local_port != dst_port || s->remote_port != src_port) continue;

        if (s->state == SOCKET_STATE_SYN_SENT) {
            if ((flags & (AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK |
                          AKAOS_TCP_FLAG_RST | AKAOS_TCP_FLAG_FIN)) !=
                (AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK) ||
                ack != s->seq || payload_len != 0U) return;
            s->ack = seq + 1U;
            s->tcp_acked_seq = ack;
            s->state = SOCKET_STATE_ESTABLISHED;
            send_tcp_packet(s, AKAOS_TCP_FLAG_ACK, 0, 0);
            return;
        }
        if (s->state != SOCKET_STATE_SYN_RECV &&
            s->state != SOCKET_STATE_ESTABLISHED) return;
        if (seq != s->ack) return;
        if (flags & AKAOS_TCP_FLAG_RST) {
            if (flags & (AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_FIN)) return;
            s->state = SOCKET_STATE_CLOSED;
            return;
        }
        if (!(flags & AKAOS_TCP_FLAG_ACK) || (flags & AKAOS_TCP_FLAG_SYN)) return;
        /* Unsigned distances handle sequence wrap as long as outstanding
         * data is less than half the sequence space. */
        if ((uint32_t)(ack - s->tcp_acked_seq) >
            (uint32_t)(s->seq - s->tcp_acked_seq)) return;
        if (s->state == SOCKET_STATE_SYN_RECV && ack != s->seq) return;
        if (payload_len > XSCORE_SOCKET_BUF_SIZE - s->rx_count) return;

        s->tcp_acked_seq = ack;
        s->state = SOCKET_STATE_ESTABLISHED;
        for (uint32_t b = 0; b < payload_len; ++b) {
            s->rx_buf[s->rx_head] = payload[b];
            s->rx_head = (s->rx_head + 1U) % XSCORE_SOCKET_BUF_SIZE;
            ++s->rx_count;
        }
        s->ack += payload_len;
        if (flags & AKAOS_TCP_FLAG_FIN) {
            ++s->ack;
            s->state = SOCKET_STATE_CLOSE_WAIT;
        }
        if (payload_len || (flags & AKAOS_TCP_FLAG_FIN))
            send_tcp_packet(s, AKAOS_TCP_FLAG_ACK, 0, 0);
        return;
    }

    if ((flags & (AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK |
                  AKAOS_TCP_FLAG_RST | AKAOS_TCP_FLAG_FIN)) != AKAOS_TCP_FLAG_SYN ||
        payload_len != 0U) return;
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        struct xscore_socket *s = &socket_pool[i];
        if (!s->in_use || s->domain != AKAOS_AF_INET ||
            s->type != AKAOS_SOCK_STREAM || s->protocol != AKAOS_IPPROTO_TCP ||
            s->state != SOCKET_STATE_LISTEN || s->local_port != dst_port ||
            s->local_ip != dst_ip) continue;
        if (s->pending_socket_id >= 0) return;
        for (int c = 0; c < XSCORE_SOCKET_MAX; ++c) {
            if (socket_pool[c].in_use) continue;
            struct xscore_socket *cs = &socket_pool[c];
            memset(cs, 0, sizeof(*cs));
            cs->in_use = 1;
            cs->domain = AKAOS_AF_INET;
            cs->type = AKAOS_SOCK_STREAM;
            cs->protocol = AKAOS_IPPROTO_TCP;
            cs->local_ip = dst_ip;
            cs->local_port = dst_port;
            cs->remote_ip = src_ip;
            cs->remote_port = src_port;
            cs->seq = 5000;
            cs->tcp_acked_seq = cs->seq;
            cs->ack = seq + 1U;
            cs->state = SOCKET_STATE_SYN_RECV;
            cs->pending_socket_id = -1;
            cs->peer_socket_id = -1;
            xscore_spinlock_init(&cs->rx_consumer_lock);
            send_tcp_packet(cs, AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK, 0, 0);
            ++cs->seq;
            s->pending_socket_id = c;
            return;
        }
        return;
    }
}

void xscore_socket_demux_tcp(const void *packet, uint32_t len,
                             uint32_t src_ip, uint32_t dst_ip) {
    uintptr_t flags = xscore_spin_lock_irqsave(&socket_pool_lock);
    socket_demux_tcp_locked(packet, len, src_ip, dst_ip);
    xscore_spin_unlock_irqrestore(&socket_pool_lock, flags);
}

static void sock_append_str(char *buf, size_t cap, size_t *used, const char *s) {
    if (!buf || !used || !s) return;
    while (*s && *used + 1 < cap) {
        buf[(*used)++] = *s++;
    }
    buf[*used < cap ? *used : cap - 1] = '\0';
}

static void sock_append_u64(char *buf, size_t cap, size_t *used, uint64_t val) {
    char digits[24];
    size_t count = 0;
    if (val == 0) digits[count++] = '0';
    while (val > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (count > 0 && *used + 1 < cap) {
        buf[(*used)++] = digits[--count];
    }
    buf[*used < cap ? *used : cap - 1] = '\0';
}

static void sock_append_hex(char *buf, size_t cap, size_t *used, uint32_t val, int width) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = width - 1; i >= 0; --i) {
        if (*used + 1 < cap) {
            buf[(*used)++] = hex[(val >> (i * 4)) & 0xF];
        }
    }
    buf[*used < cap ? *used : cap - 1] = '\0';
}

static uint8_t tcp_state_to_proc(enum xscore_socket_state st) {
    switch (st) {
        case SOCKET_STATE_ESTABLISHED: return 0x01;
        case SOCKET_STATE_SYN_SENT:    return 0x02;
        case SOCKET_STATE_SYN_RECV:    return 0x03;
        case SOCKET_STATE_FIN_WAIT:    return 0x04;
        case SOCKET_STATE_TIME_WAIT:   return 0x06;
        case SOCKET_STATE_CLOSED:      return 0x07;
        case SOCKET_STATE_CLOSE_WAIT:  return 0x08;
        case SOCKET_STATE_LISTEN:      return 0x0A;
        default:                       return 0x07;
    }
}

size_t xscore_socket_format_proc_tcp(char *buf, size_t capacity) {
    if (!buf || capacity == 0) return 0;
    size_t used = 0;
    sock_append_str(buf, capacity, &used,
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");

    int sl = 0;
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (!socket_pool[i].in_use || socket_pool[i].protocol != AKAOS_IPPROTO_TCP) continue;

        sock_append_str(buf, capacity, &used, "   ");
        sock_append_u64(buf, capacity, &used, (uint64_t)sl++);
        sock_append_str(buf, capacity, &used, ": ");

        sock_append_hex(buf, capacity, &used, socket_pool[i].local_ip, 8);
        sock_append_str(buf, capacity, &used, ":");
        sock_append_hex(buf, capacity, &used, (uint32_t)socket_pool[i].local_port, 4);
        sock_append_str(buf, capacity, &used, " ");

        sock_append_hex(buf, capacity, &used, socket_pool[i].remote_ip, 8);
        sock_append_str(buf, capacity, &used, ":");
        sock_append_hex(buf, capacity, &used, (uint32_t)socket_pool[i].remote_port, 4);
        sock_append_str(buf, capacity, &used, " ");

        sock_append_hex(buf, capacity, &used, (uint32_t)tcp_state_to_proc(socket_pool[i].state), 2);
        sock_append_str(buf, capacity, &used, " 00000000:");
        sock_append_hex(buf, capacity, &used, socket_pool[i].rx_count, 8);
        sock_append_str(buf, capacity, &used, " 00:00000000 00000000     0        0 ");
        sock_append_u64(buf, capacity, &used, (uint64_t)(1000 + i));
        sock_append_str(buf, capacity, &used, " 1 0 0 0 0\n");
    }
    return used;
}

size_t xscore_socket_format_proc_udp(char *buf, size_t capacity) {
    if (!buf || capacity == 0) return 0;
    size_t used = 0;
    sock_append_str(buf, capacity, &used,
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n");

    int sl = 0;
    for (int i = 0; i < XSCORE_SOCKET_MAX; ++i) {
        if (!socket_pool[i].in_use || socket_pool[i].protocol != AKAOS_IPPROTO_UDP) continue;

        sock_append_str(buf, capacity, &used, "   ");
        sock_append_u64(buf, capacity, &used, (uint64_t)sl++);
        sock_append_str(buf, capacity, &used, ": ");

        sock_append_hex(buf, capacity, &used, socket_pool[i].local_ip, 8);
        sock_append_str(buf, capacity, &used, ":");
        sock_append_hex(buf, capacity, &used, (uint32_t)socket_pool[i].local_port, 4);
        sock_append_str(buf, capacity, &used, " ");

        sock_append_hex(buf, capacity, &used, socket_pool[i].remote_ip, 8);
        sock_append_str(buf, capacity, &used, ":");
        sock_append_hex(buf, capacity, &used, (uint32_t)socket_pool[i].remote_port, 4);
        sock_append_str(buf, capacity, &used, " 07 00000000:");
        sock_append_hex(buf, capacity, &used, socket_pool[i].rx_count, 8);
        sock_append_str(buf, capacity, &used, " 00:00000000 00000000     0        0 ");
        sock_append_u64(buf, capacity, &used, (uint64_t)(1000 + i));
        sock_append_str(buf, capacity, &used, " 2 0 0\n");
    }
    return used;
}
