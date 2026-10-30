#ifndef AKAOS_LIBC_NET_IF_H
#define AKAOS_LIBC_NET_IF_H

#include <sys/socket.h>

#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ
#define IFHWADDRLEN 6
#define IFF_UP 0x0001
#define IFF_BROADCAST 0x0002
#define IFF_LOOPBACK 0x0008
#define IFF_RUNNING 0x0040
#define IFF_MULTICAST 0x1000

struct ifmap {
    unsigned long mem_start;
    unsigned long mem_end;
    unsigned short base_addr;
    unsigned char irq;
    unsigned char dma;
    unsigned char port;
};

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
        short ifru_flags;
        int ifru_ifindex;
        int ifru_metric;
        int ifru_mtu;
        struct ifmap ifru_map;
        char ifru_slave[IFNAMSIZ];
        char ifru_newname[IFNAMSIZ];
        char *ifru_data;
    } ifr_ifru;
};

struct ifconf {
    int ifc_len;
    union {
        char *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};

#define ifr_addr ifr_ifru.ifru_addr
#define ifr_dstaddr ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask ifr_ifru.ifru_netmask
#define ifr_hwaddr ifr_ifru.ifru_hwaddr
#define ifr_flags ifr_ifru.ifru_flags
#define ifr_ifindex ifr_ifru.ifru_ifindex
#define ifr_metric ifr_ifru.ifru_metric
#define ifr_mtu ifr_ifru.ifru_mtu
#define ifr_map ifr_ifru.ifru_map
#define ifr_slave ifr_ifru.ifru_slave
#define ifr_newname ifr_ifru.ifru_newname
#define ifr_data ifr_ifru.ifru_data
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

#ifdef __cplusplus
extern "C" {
#endif

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *list);

#ifdef __cplusplus
}
#endif

#endif
