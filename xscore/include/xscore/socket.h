#ifndef XSCORE_SOCKET_H
#define XSCORE_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <xscore/net.h>
#include <xscore/smp.h>
#include <akaos/syscall.h>

#define XSCORE_SOCKET_MAX 256
#define XSCORE_SOCKET_BUF_SIZE 8192

#define AKAOS_AF_UNIX 1
#define AKAOS_AF_INET 2
#define AKAOS_UNIX_PATH_MAX 108

#define XSCORE_UNIX_RIGHTS_MAX 4U
#define XSCORE_UNIX_RIGHTS_QUEUE 8U

struct xscore_file;
struct xscore_fd_table;

struct xscore_unix_rights_record {
    uint64_t stream_position;
    uint32_t file_count;
    struct xscore_file *files[XSCORE_UNIX_RIGHTS_MAX];
};

#define AKAOS_SOCK_STREAM 1
#define AKAOS_SOCK_DGRAM  2

enum xscore_socket_state {
    SOCKET_STATE_CLOSED = 0,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTEN,
    SOCKET_STATE_SYN_SENT,
    SOCKET_STATE_SYN_RECV,
    SOCKET_STATE_ESTABLISHED,
    SOCKET_STATE_FIN_WAIT,
    SOCKET_STATE_CLOSE_WAIT,
    SOCKET_STATE_TIME_WAIT
};

struct akaos_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
} __attribute__((packed));

struct akaos_sockaddr_un {
    uint16_t sun_family;
    char sun_path[AKAOS_UNIX_PATH_MAX];
} __attribute__((packed));

struct xscore_socket {
    uint32_t in_use;
    int domain;
    int type;
    int protocol;
    enum xscore_socket_state state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    uint32_t seq;
    uint32_t ack;
    uint32_t read_shutdown;
    uint32_t write_shutdown;
    uint32_t peer_write_shutdown;

    uint8_t rx_buf[8192];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
    uint64_t rx_read_position;
    uint64_t rx_write_position;
    uint32_t rights_record_count;
    struct xscore_unix_rights_record
        rights_records[XSCORE_UNIX_RIGHTS_QUEUE];
    xscore_spinlock_t rx_consumer_lock;

    int pending_socket_id;
    int peer_socket_id;
    struct akaos_ucred listener_credentials;
    struct akaos_ucred peer_credentials;
    uint32_t listener_credentials_valid;
    uint32_t peer_credentials_valid;
    char local_path[AKAOS_UNIX_PATH_MAX];
    char remote_path[AKAOS_UNIX_PATH_MAX];
};

void xscore_socket_initialize(void);
int64_t xscore_socket_create(int domain, int type, int protocol);
int64_t xscore_socket_connect(int sock_id, uint32_t ip, uint16_t port);
int64_t xscore_socket_connect_unix(int sock_id, const char *path);
int64_t xscore_socket_bind(int sock_id, uint32_t ip, uint16_t port);
int64_t xscore_socket_bind_unix(int sock_id, const char *path);
int64_t xscore_socket_listen(int sock_id, int backlog);
int64_t xscore_socket_accept(int sock_id, uint32_t *remote_ip, uint16_t *remote_port);
int64_t xscore_socket_get_peer_credentials(int sock_id,
                                           struct akaos_ucred *credentials);
int64_t xscore_socket_get_name(int sock_id, int peer, int *domain,
                               uint32_t *ip, uint16_t *port,
                               char *path, size_t path_capacity,
                               size_t *path_length);
int64_t xscore_socket_get_option(int sock_id, int level, int option,
                                 uint32_t *value);
int64_t xscore_socket_set_option(int sock_id, int level, int option,
                                 uint32_t value);
int64_t xscore_socket_shutdown(int sock_id, int how);
int64_t xscore_socket_send(int sock_id, const void *buf, size_t len);
int64_t xscore_socket_sendto(int sock_id, const void *buf, size_t len, uint32_t dest_ip, uint16_t dest_port);
int64_t xscore_socket_receive(int sock_id, void *buf, size_t len);
/* A successful controlled send transfers the supplied file references into
 * the peer's receive queue. A failed send leaves ownership with the caller. */
int64_t xscore_socket_sendmsg(int sock_id, const void *buf, size_t len,
                              struct xscore_file *const *files,
                              size_t file_count);
int64_t xscore_socket_recvmsg(int sock_id, struct xscore_fd_table *table,
                              void *buf, size_t capacity, int *received_fds,
                              size_t fd_capacity, size_t *data_length,
                              size_t *fd_count, int *message_flags);
size_t xscore_socket_close_collect(int sock_id,
                                   struct xscore_file **released_files,
                                   size_t capacity);
int64_t xscore_socket_recvfrom(int sock_id, void *buf, size_t len, uint32_t *src_ip, uint16_t *src_port);
int xscore_socket_poll_readable(int sock_id);
int xscore_socket_poll_writable(int sock_id);
int xscore_socket_domain(int sock_id);
void xscore_socket_close(int sock_id);

void xscore_socket_demux_udp(const void *packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip);
void xscore_socket_demux_tcp(const void *packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip);

size_t xscore_socket_format_proc_tcp(char *buf, size_t capacity);
size_t xscore_socket_format_proc_udp(char *buf, size_t capacity);

#endif
