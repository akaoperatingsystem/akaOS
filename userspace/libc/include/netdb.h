#ifndef AKAOS_LIBC_NETDB_H
#define AKAOS_LIBC_NETDB_H

#include <stdint.h>
#include <netinet/in.h>

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};

/* IPv4 addrinfo subset used by POSIX networking clients.  The native network
 * stack is intentionally bounded to AF_INET; unsupported families return a
 * stable EAI_FAMILY result instead of silently producing a host-only record. */
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE    0x0001
#define AI_CANONNAME  0x0002
#define AI_NUMERICHOST 0x0004
#define AI_NUMERICSERV 0x0008

#define EAI_BADFLAGS  (-1)
#define EAI_NONAME    (-2)
#define EAI_AGAIN     (-3)
#define EAI_FAIL      (-4)
#define EAI_FAMILY    (-6)
#define EAI_SOCKTYPE  (-7)
#define EAI_SERVICE   (-8)
#define EAI_SYSTEM    (-11)
#define EAI_OVERFLOW  (-12)
#define EAI_MEMORY    (-10)

#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#define NO_ADDRESS     NO_DATA

#define NI_NOFQDN 0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD 0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM 0x10
#define NI_WITHSCOPEID 0x20
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

#define h_addr h_addr_list[0]

#ifdef __cplusplus
extern "C" {
#endif

int *__h_errno_location(void);
#define h_errno (*__h_errno_location())

struct hostent *gethostbyname(const char *name);
struct servent *getservbyname(const char *name, const char *protocol);
struct servent *getservbyport(int port, const char *protocol);
void setservent(int stay_open);
void endservent(void);
struct servent *getservent(void);
int getservbyname_r(const char *name, const char *protocol,
                    struct servent *entry, char *buffer,
                    size_t buffer_size, struct servent **result);
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *info);
const char *gai_strerror(int error_code);
int getnameinfo(const struct sockaddr *address, socklen_t address_length,
                char *host, socklen_t host_length, char *service,
                socklen_t service_length, int flags);

#ifdef __cplusplus
}
#endif

#endif
