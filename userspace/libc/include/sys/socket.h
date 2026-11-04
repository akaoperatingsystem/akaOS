#ifndef AKAOS_LIBC_SYS_SOCKET_H
#define AKAOS_LIBC_SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define AF_INET 2
#define AF_INET6 10
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_LOCAL AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define UNIX_PATH_MAX 108

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_SEQPACKET 5
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC 0x80000
#define SOMAXCONN 128
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_DOMAIN 39
#define SO_PROTOCOL 38
#define SO_PEERCRED 17
#define SO_NOSIGPIPE 0x1022
#define SO_NREAD 0x1020
#define SO_PROTOCOL_INFO 0x2005
#define SO_ACCEPTCONN 30

#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2
#define MSG_OOB 0x01
#define MSG_PEEK 0x02
#define MSG_DONTROUTE 0x04
#define MSG_CTRUNC 0x08
#define MSG_PROXY 0x10
#define MSG_TRUNC 0x20
#define MSG_DONTWAIT 0x40
#define MSG_EOR 0x80
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000
#define MSG_MORE 0x8000
#define MSG_CMSG_CLOEXEC 0x40000000

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_storage {
    uint16_t ss_family;
    uint8_t __ss_padding[118];
    uint64_t __ss_align;
};

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[UNIX_PATH_MAX];
};

struct ucred {
    pid_t pid;
    uid_t uid;
    gid_t gid;
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(length) \
    (((size_t)(length) + sizeof(size_t) - 1U) & ~(sizeof(size_t) - 1U))
#define CMSG_DATA(header) \
    ((unsigned char *)(header) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_LEN(length) \
    (CMSG_ALIGN(sizeof(struct cmsghdr)) + (size_t)(length))
#define CMSG_SPACE(length) \
    (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(length))
#define CMSG_FIRSTHDR(message) \
    (((message) && (message)->msg_control && \
      (message)->msg_controllen >= sizeof(struct cmsghdr)) \
         ? (struct cmsghdr *)(message)->msg_control : (struct cmsghdr *)0)
/* The next control header is computed with a pure expression so that
 * including this header never introduces a declaration after a statement,
 * which upstream C sources compile with strict warnings. */
#define CMSG_NXTHDR(message, header)                                       \
    (((message) && (message)->msg_control && (header) &&                    \
      (header)->cmsg_len >= CMSG_LEN(0) &&                                  \
      ((size_t)((const unsigned char *)(header) +                           \
                CMSG_ALIGN((header)->cmsg_len) + sizeof(struct cmsghdr)) <= \
       (size_t)((const unsigned char *)(message)->msg_control +             \
                (message)->msg_controllen)))                               \
         ? (struct cmsghdr *)(void *)((unsigned char *)(uintptr_t)(header) + \
                                     CMSG_ALIGN((header)->cmsg_len))        \
         : (struct cmsghdr *)0)

#ifdef __cplusplus
extern "C" {
#endif
int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int socket_vector[2]);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int option, const void *optval,
               socklen_t optlen);
int shutdown(int sockfd, int how);
int getsockopt(int sockfd, int level, int option, void *optval,
               socklen_t *optlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *message, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *message, int flags);
#ifdef __cplusplus
}
#endif

#endif
