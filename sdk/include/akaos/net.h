#ifndef AKAOS_NET_H
#define AKAOS_NET_H

#include <stdint.h>

#define AKAOS_NET_DEVICE_PATH "/dev/net"
#ifndef AKAOS_SIOCGIFSTATS
#define AKAOS_SIOCGIFSTATS    UINT32_C(0x89f0)
#endif

struct akaos_net_stats {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint8_t mac[6];
    uint32_t ip;
};

struct akaos_sock_io_args {
    uint32_t len;
    uint32_t ip;
    uint16_t port;
    uint16_t reserved;
};

#endif
