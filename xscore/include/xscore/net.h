#ifndef XSCORE_NET_H
#define XSCORE_NET_H

#include <stdint.h>
#include <stddef.h>
#include <akaos/net.h>

#define AKAOS_ETH_TYPE_IP  0x0800
#define AKAOS_ETH_TYPE_ARP 0x0806

#define AKAOS_IP_PROTO_ICMP 1
#define AKAOS_IP_PROTO_TCP  6
#define AKAOS_IP_PROTO_UDP  17

#define AKAOS_ICMP_ECHO_REPLY   0
#define AKAOS_ICMP_ECHO_REQUEST 8

struct akaos_eth_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

struct akaos_arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_len;
    uint8_t proto_len;
    uint16_t opcode;
    uint8_t src_mac[6];
    uint32_t src_ip;
    uint8_t dst_mac[6];
    uint32_t dst_ip;
} __attribute__((packed));

struct akaos_ip_hdr {
    uint8_t v_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct akaos_icmp_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct akaos_udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct akaos_tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed));

#define AKAOS_TCP_FLAG_FIN 0x01
#define AKAOS_TCP_FLAG_SYN 0x02
#define AKAOS_TCP_FLAG_RST 0x04
#define AKAOS_TCP_FLAG_PSH 0x08
#define AKAOS_TCP_FLAG_ACK 0x10
#define AKAOS_TCP_FLAG_URG 0x20



void xscore_net_initialize(void);
int64_t xscore_net_transmit(const void *buf, uint64_t len);
int64_t xscore_net_receive(void *buf, uint64_t max_len);
int xscore_net_poll_readable(void);
int xscore_net_poll_writable(void);
void xscore_net_get_stats(struct akaos_net_stats *stats);
void xscore_net_queue_rx(const void *buf, uint32_t len);
void xscore_net_poll_loopback(void);

int xscore_virtio_net_initialize(void);
int xscore_virtio_net_present(void);
int64_t xscore_virtio_net_transmit(const void *buf, uint64_t len);
void xscore_virtio_net_poll_rx(void);
void xscore_virtio_net_interrupt(void);
void xscore_virtio_net_get_mac(uint8_t mac[6]);

size_t xscore_net_format_proc_route(char *buf, size_t capacity);
size_t xscore_net_format_proc_arp(char *buf, size_t capacity);
size_t xscore_net_format_proc_dev(char *buf, size_t capacity);

#endif
