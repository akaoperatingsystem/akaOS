#ifndef AKAOS_LIBC_NETINET_IN_H
#define AKAOS_LIBC_NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IP 0
#define IPPROTO_IPV6 41
#define IP_TOS 1
#define IP_TTL 2
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36
#define IP_ADD_SOURCE_MEMBERSHIP 39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#define IPV6_UNICAST_HOPS 16
#define IPV6_MULTICAST_IF 17
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_JOIN_GROUP 20
#define IPV6_LEAVE_GROUP 21
#define IPV6_V6ONLY 26
#define IPV6_TCLASS 67
#define IPV6_RECVPKTINFO 49
#define IPV6_PKTINFO 50
#define IPV6_RECVHOPLIMIT 51
#define IPV6_HOPLIMIT 52

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IN_MULTICAST(address) \
    ((((uint32_t)(address)) & UINT32_C(0xf0000000)) == UINT32_C(0xe0000000))

/* The conventional BSD spelling of the IPv4 address type. */
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in6_addr {
    uint8_t s6_addr[16];
};

#define IN6_IS_ADDR_V4MAPPED(address) \
    (((const uint8_t *)(address)->s6_addr)[0] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[1] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[2] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[3] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[4] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[5] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[6] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[7] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[8] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[9] == 0U && \
     ((const uint8_t *)(address)->s6_addr)[10] == 0xffU && \
     ((const uint8_t *)(address)->s6_addr)[11] == 0xffU)

#define IN6_IS_ADDR_UNSPECIFIED(address) \
    ((address)->s6_addr[0] == 0U && (address)->s6_addr[1] == 0U && \
     (address)->s6_addr[2] == 0U && (address)->s6_addr[3] == 0U && \
     (address)->s6_addr[4] == 0U && (address)->s6_addr[5] == 0U && \
     (address)->s6_addr[6] == 0U && (address)->s6_addr[7] == 0U && \
     (address)->s6_addr[8] == 0U && (address)->s6_addr[9] == 0U && \
     (address)->s6_addr[10] == 0U && (address)->s6_addr[11] == 0U && \
     (address)->s6_addr[12] == 0U && (address)->s6_addr[13] == 0U && \
     (address)->s6_addr[14] == 0U && (address)->s6_addr[15] == 0U)
#define IN6_IS_ADDR_LOOPBACK(address) \
    (IN6_IS_ADDR_UNSPECIFIED(address) == 0 && \
     (address)->s6_addr[0] == 0U && (address)->s6_addr[1] == 0U && \
     (address)->s6_addr[2] == 0U && (address)->s6_addr[3] == 0U && \
     (address)->s6_addr[4] == 0U && (address)->s6_addr[5] == 0U && \
     (address)->s6_addr[6] == 0U && (address)->s6_addr[7] == 0U && \
     (address)->s6_addr[8] == 0U && (address)->s6_addr[9] == 0U && \
     (address)->s6_addr[10] == 0U && (address)->s6_addr[11] == 0U && \
     (address)->s6_addr[12] == 0U && (address)->s6_addr[13] == 0U && \
     (address)->s6_addr[14] == 0U && (address)->s6_addr[15] == 1U)
#define IN6_IS_ADDR_MULTICAST(address) \
    (((const uint8_t *)(address)->s6_addr)[0] == 0xffU)
#define IN6_IS_ADDR_LINKLOCAL(address) \
    ((address)->s6_addr[0] == 0xfeU && \
     (((address)->s6_addr[1] & 0xc0U) == 0x80U))
#define IN6_IS_ADDR_SITELOCAL(address) \
    ((address)->s6_addr[0] == 0xfeU && \
     (((address)->s6_addr[1] & 0xc0U) == 0xc0U))
#define IN6_IS_ADDR_MC_SCOPE(address, scope) \
    (IN6_IS_ADDR_MULTICAST(address) && \
     (((address)->s6_addr[1] & 0x0fU) == (scope)))
#define IN6_IS_ADDR_MC_NODELOCAL(address) IN6_IS_ADDR_MC_SCOPE(address, 1U)
#define IN6_IS_ADDR_MC_LINKLOCAL(address) IN6_IS_ADDR_MC_SCOPE(address, 2U)
#define IN6_IS_ADDR_MC_SITELOCAL(address) IN6_IS_ADDR_MC_SCOPE(address, 5U)
#define IN6_IS_ADDR_MC_ORGLOCAL(address) IN6_IS_ADDR_MC_SCOPE(address, 8U)
#define IN6_IS_ADDR_MC_GLOBAL(address) IN6_IS_ADDR_MC_SCOPE(address, 14U)

struct sockaddr_in6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

struct in6_pktinfo {
    struct in6_addr ipi6_addr;
    unsigned int ipi6_ifindex;
};

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};

struct ip_mreq_source {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
    struct in_addr imr_sourceaddr;
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int ipv6mr_interface;
};

#define INADDR_ANY       0x00000000
#define INADDR_LOOPBACK  0x7f000001
#define INADDR_BROADCAST UINT32_C(0xffffffff)
#define INADDR_NONE      UINT32_C(0xffffffff)

extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;
#define IN6ADDR_ANY_INIT { { 0 } }
#define IN6ADDR_LOOPBACK_INIT { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } }

#ifdef __cplusplus
extern "C" {
#endif

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);

#ifdef __cplusplus
}
#endif

#endif
