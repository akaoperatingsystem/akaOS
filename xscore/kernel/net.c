#include <xscore/net.h>
#include <xscore/socket.h>
#include <xscore/serial.h>
#include <stdint.h>
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);

#define NET_PACKET_MAX 1536
#define NET_RING_SLOTS 16

struct net_packet {
    uint8_t data[NET_PACKET_MAX];
    uint32_t len;
};

static struct net_packet rx_ring[NET_RING_SLOTS];
static uint32_t rx_head = 0;
static uint32_t rx_tail = 0;

static struct akaos_net_stats net_stats;

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

void xscore_net_send_gratuitous_arp(void) {
    if (!xscore_virtio_net_present()) return;
    uint8_t arp_buf[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_arp_hdr)];
    struct akaos_eth_hdr *r_eth = (struct akaos_eth_hdr *)arp_buf;
    struct akaos_arp_hdr *r_arp = (struct akaos_arp_hdr *)(arp_buf + sizeof(struct akaos_eth_hdr));

    memset(r_eth->dst, 0xff, 6);
    memcpy(r_eth->src, net_stats.mac, 6);
    r_eth->type = 0x0608; /* 0x0806 in network byte order */

    r_arp->hw_type = 0x0100; /* 0x0001 in network byte order */
    r_arp->proto_type = 0x0008; /* 0x0800 in network byte order */
    r_arp->hw_len = 6;
    r_arp->proto_len = 4;
    r_arp->opcode = 0x0100; /* ARP Request announcement */
    memcpy(r_arp->src_mac, net_stats.mac, 6);
    r_arp->src_ip = net_stats.ip;
    memset(r_arp->dst_mac, 0x00, 6);
    r_arp->dst_ip = net_stats.ip;

    xscore_virtio_net_transmit(arp_buf, sizeof(arp_buf));
}

void xscore_net_initialize(void) {
    rx_head = 0; rx_tail = 0;
    memset(&net_stats, 0, sizeof(net_stats));
    net_stats.mac[0] = 0x52;
    net_stats.mac[1] = 0x54;
    net_stats.mac[2] = 0x00;
    net_stats.mac[3] = 0x12;
    net_stats.mac[4] = 0x34;
    net_stats.mac[5] = 0x56;
    /* 10.0.2.15 */
    net_stats.ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);

    if (xscore_virtio_net_present()) {
        xscore_virtio_net_get_mac(net_stats.mac);
        xscore_net_send_gratuitous_arp();
    }
    xscore_linux_net_bridge_init();
    xscore_socket_initialize();
}

static void queue_rx_packet(const uint8_t *data, uint32_t len) {
    if (len == 0 || len > NET_PACKET_MAX) return;
    uint32_t next = (rx_head + 1) % NET_RING_SLOTS;
    if (next == rx_tail) return; /* Ring full */

    memcpy(rx_ring[rx_head].data, data, len);
    rx_ring[rx_head].len = len;
    rx_head = next;
    net_stats.rx_packets++;
    net_stats.rx_bytes += len;
}

void xscore_net_queue_rx(const void *buf, uint32_t len) {
    if (!buf || len == 0) return;

    if (len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_arp_hdr)) {
        const struct akaos_eth_hdr *eth = (const struct akaos_eth_hdr *)buf;
        uint16_t eth_type = ((eth->type >> 8) & 0xff) | ((eth->type & 0xff) << 8);
        if (eth_type == AKAOS_ETH_TYPE_ARP) {
            const struct akaos_arp_hdr *arp = (const struct akaos_arp_hdr *)((const uint8_t *)buf + sizeof(struct akaos_eth_hdr));
            uint16_t op = ((arp->opcode >> 8) & 0xff) | ((arp->opcode & 0xff) << 8);
            if (op == 1 && arp->dst_ip == net_stats.ip) {
                /* Generate and send ARP Reply */
                uint8_t reply_buf[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_arp_hdr)];
                struct akaos_eth_hdr *r_eth = (struct akaos_eth_hdr *)reply_buf;
                struct akaos_arp_hdr *r_arp = (struct akaos_arp_hdr *)(reply_buf + sizeof(struct akaos_eth_hdr));

                memcpy(r_eth->dst, arp->src_mac, 6);
                memcpy(r_eth->src, net_stats.mac, 6);
                r_eth->type = eth->type;

                r_arp->hw_type = arp->hw_type;
                r_arp->proto_type = arp->proto_type;
                r_arp->hw_len = 6;
                r_arp->proto_len = 4;
                r_arp->opcode = 0x0200; /* 0x0002 in network byte order */
                memcpy(r_arp->src_mac, net_stats.mac, 6);
                r_arp->src_ip = arp->dst_ip;
                memcpy(r_arp->dst_mac, arp->src_mac, 6);
                r_arp->dst_ip = arp->src_ip;

                if (xscore_virtio_net_present()) {
                    xscore_virtio_net_transmit(reply_buf, sizeof(reply_buf));
                }
                return;
            }
        } else if (eth_type == AKAOS_ETH_TYPE_IP && len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr)) {
            const struct akaos_ip_hdr *ip = (const struct akaos_ip_hdr *)((const uint8_t *)buf + sizeof(struct akaos_eth_hdr));
            uint32_t ip_hdr_len = (uint32_t)((ip->v_ihl & 0x0f) * 4);
            /* Ethernet frames are padded to their minimum wire size.  The
             * padding is not part of the IPv4 payload; using the raw frame
             * length here leaks trailing zeroes into TCP/UDP streams (for
             * example, curl responses).  Trust the bounded IPv4 total-length
             * field when dispatching transport data. */
            uint16_t wire_total_len = (uint16_t)((ip->total_len >> 8) |
                                                 ((ip->total_len & 0xff) << 8));
            if (ip_hdr_len < sizeof(struct akaos_ip_hdr) || ip_hdr_len > 60 ||
                wire_total_len < ip_hdr_len ||
                len < sizeof(struct akaos_eth_hdr) + ip_hdr_len) {
                goto queue_packet;
            }
            if (len >= sizeof(struct akaos_eth_hdr) + ip_hdr_len) {
                const uint8_t *l4_data = (const uint8_t *)buf + sizeof(struct akaos_eth_hdr) + ip_hdr_len;
                uint32_t l4_len = len - sizeof(struct akaos_eth_hdr) - ip_hdr_len;
                uint32_t declared_l4_len = (uint32_t)wire_total_len - ip_hdr_len;
                if (declared_l4_len < l4_len) l4_len = declared_l4_len;
                if (ip->proto == 17) {
                    xscore_socket_demux_udp(l4_data, l4_len, ip->src_ip, ip->dst_ip);
                } else if (ip->proto == 6) {
                    xscore_socket_demux_tcp(l4_data, l4_len, ip->src_ip, ip->dst_ip);
                }
            }
        }
    }

queue_packet:
    queue_rx_packet((const uint8_t *)buf, len);
}

static void process_transmitted_frame(const uint8_t *frame, uint32_t len) {
    if (len < sizeof(struct akaos_eth_hdr)) return;

    const struct akaos_eth_hdr *eth = (const struct akaos_eth_hdr *)frame;
    uint16_t eth_type = ((eth->type >> 8) & 0xff) | ((eth->type & 0xff) << 8);

    if (eth_type == AKAOS_ETH_TYPE_ARP && len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_arp_hdr)) {
        const struct akaos_arp_hdr *arp = (const struct akaos_arp_hdr *)(frame + sizeof(struct akaos_eth_hdr));
        uint16_t op = ((arp->opcode >> 8) & 0xff) | ((arp->opcode & 0xff) << 8);
        if (op == 1) { /* ARP Request */
            /* Generate ARP Reply */
            uint8_t reply_buf[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_arp_hdr)];
            struct akaos_eth_hdr *r_eth = (struct akaos_eth_hdr *)reply_buf;
            struct akaos_arp_hdr *r_arp = (struct akaos_arp_hdr *)(reply_buf + sizeof(struct akaos_eth_hdr));

            memcpy(r_eth->dst, eth->src, 6);
            memcpy(r_eth->src, net_stats.mac, 6);
            r_eth->type = eth->type;

            r_arp->hw_type = arp->hw_type;
            r_arp->proto_type = arp->proto_type;
            r_arp->hw_len = 6;
            r_arp->proto_len = 4;
            r_arp->opcode = (2 >> 8) | ((2 & 0xff) << 8); /* Reply */
            memcpy(r_arp->src_mac, net_stats.mac, 6);
            r_arp->src_ip = arp->dst_ip;
            memcpy(r_arp->dst_mac, arp->src_mac, 6);
            r_arp->dst_ip = arp->src_ip;

            queue_rx_packet(reply_buf, sizeof(reply_buf));
        }
    } else if (eth_type == AKAOS_ETH_TYPE_IP && len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr)) {
        const struct akaos_ip_hdr *ip = (const struct akaos_ip_hdr *)(frame + sizeof(struct akaos_eth_hdr));
        if (ip->proto == AKAOS_IP_PROTO_ICMP &&
            len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr)) {
            const struct akaos_icmp_hdr *icmp = (const struct akaos_icmp_hdr *)(
                frame + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));

            if (icmp->type == AKAOS_ICMP_ECHO_REQUEST) {
                uint32_t gateway_ip = (10) | (0 << 8) | (2 << 16) | (2 << 24);
                if (ip->ttl == 1 && ip->dst_ip != net_stats.ip && ip->dst_ip != gateway_ip) {
                    /* Generate ICMP Time Exceeded (Type 11, Code 0) from Gateway */
                    uint8_t time_exc_buf[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) +
                                         sizeof(struct akaos_icmp_hdr) + sizeof(struct akaos_ip_hdr) + 8];
                    struct akaos_eth_hdr *r_eth = (struct akaos_eth_hdr *)time_exc_buf;
                    struct akaos_ip_hdr *r_ip = (struct akaos_ip_hdr *)(time_exc_buf + sizeof(struct akaos_eth_hdr));
                    struct akaos_icmp_hdr *r_icmp = (struct akaos_icmp_hdr *)(
                        time_exc_buf + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));
                    uint8_t *payload = time_exc_buf + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr);

                    memcpy(r_eth->dst, eth->src, 6);
                    memcpy(r_eth->src, net_stats.mac, 6);
                    r_eth->type = eth->type;

                    r_ip->v_ihl = 0x45;
                    r_ip->tos = 0;
                    uint16_t tot_len = (uint16_t)(sizeof(time_exc_buf) - sizeof(struct akaos_eth_hdr));
                    r_ip->total_len = (uint16_t)((tot_len >> 8) | ((tot_len & 0xff) << 8));
                    r_ip->id = 0x1234;
                    r_ip->flags_frag = 0;
                    r_ip->ttl = 64;
                    r_ip->proto = AKAOS_IP_PROTO_ICMP;
                    r_ip->src_ip = gateway_ip;
                    r_ip->dst_ip = ip->src_ip;
                    r_ip->checksum = 0;
                    r_ip->checksum = net_checksum(r_ip, sizeof(struct akaos_ip_hdr));

                    r_icmp->type = 11; /* Time Exceeded */
                    r_icmp->code = 0;
                    r_icmp->checksum = 0;
                    r_icmp->id = 0;
                    r_icmp->seq = 0;
                    memcpy(payload, ip, sizeof(struct akaos_ip_hdr) + 8);
                    r_icmp->checksum = net_checksum(r_icmp, sizeof(struct akaos_icmp_hdr) + sizeof(struct akaos_ip_hdr) + 8);

                    queue_rx_packet(time_exc_buf, sizeof(time_exc_buf));
                } else {
                    /* Generate ICMP Echo Reply */
                    uint8_t reply_buf[NET_PACKET_MAX];
                    if (len <= NET_PACKET_MAX) {
                        memcpy(reply_buf, frame, len);
                        struct akaos_eth_hdr *r_eth = (struct akaos_eth_hdr *)reply_buf;
                        struct akaos_ip_hdr *r_ip = (struct akaos_ip_hdr *)(reply_buf + sizeof(struct akaos_eth_hdr));
                        struct akaos_icmp_hdr *r_icmp = (struct akaos_icmp_hdr *)(
                            reply_buf + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));

                        memcpy(r_eth->dst, eth->src, 6);
                        memcpy(r_eth->src, net_stats.mac, 6);

                        uint32_t tmp_ip = r_ip->src_ip;
                        r_ip->src_ip = r_ip->dst_ip;
                        r_ip->dst_ip = tmp_ip;
                        r_ip->checksum = 0;
                        r_ip->checksum = net_checksum(r_ip, sizeof(struct akaos_ip_hdr));

                        r_icmp->type = AKAOS_ICMP_ECHO_REPLY;
                        r_icmp->checksum = 0;
                        size_t icmp_len = len - (sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));
                        r_icmp->checksum = net_checksum(r_icmp, icmp_len);

                        queue_rx_packet(reply_buf, len);
                    }
                }
            }
        }
    }
}

static struct {
    uint8_t data[NET_PACKET_MAX];
    uint32_t len;
} loopback_ring[16];
static uint32_t loopback_head = 0;
static uint32_t loopback_tail = 0;
static int in_poll_loopback = 0;

void xscore_net_poll_loopback(void) {
    if (in_poll_loopback) return;
    in_poll_loopback = 1;
    while (loopback_tail != loopback_head) {
        uint32_t idx = loopback_tail;
        loopback_tail = (loopback_tail + 1) % 16;
        xscore_net_queue_rx(loopback_ring[idx].data, loopback_ring[idx].len);
    }
    in_poll_loopback = 0;
}

static void queue_loopback(const void *buf, uint32_t len) {
    if (!buf || len == 0 || len > NET_PACKET_MAX) return;
    uint32_t next = (loopback_head + 1) % 16;
    if (next == loopback_tail) return;
    memcpy(loopback_ring[loopback_head].data, buf, len);
    loopback_ring[loopback_head].len = len;
    loopback_head = next;
}

int64_t xscore_net_transmit(const void *buf, uint64_t len) {
    if (!buf || len == 0 || len > NET_PACKET_MAX) return 0;
    net_stats.tx_packets++;
    net_stats.tx_bytes += (uint32_t)len;

    if (len >= sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr)) {
        const struct akaos_eth_hdr *eth = (const struct akaos_eth_hdr *)buf;
        uint16_t eth_type = ((eth->type >> 8) & 0xff) | ((eth->type & 0xff) << 8);
        if (eth_type == AKAOS_ETH_TYPE_IP) {
            const struct akaos_ip_hdr *ip = (const struct akaos_ip_hdr *)((const uint8_t *)buf + sizeof(struct akaos_eth_hdr));
            uint32_t loopback_ip = (127) | (0 << 8) | (0 << 16) | (1 << 24);
            if (ip->dst_ip == loopback_ip || ip->dst_ip == net_stats.ip) {
                queue_loopback(buf, (uint32_t)len);
                return (int64_t)len;
            }
        }
    }

    if (xscore_virtio_net_present()) {
        static uint8_t frame_buf[NET_PACKET_MAX];
        memcpy(frame_buf, buf, len);
        if (len >= sizeof(struct akaos_eth_hdr)) {
            struct akaos_eth_hdr *eth = (struct akaos_eth_hdr *)frame_buf;
            memcpy(eth->src, net_stats.mac, 6);
        }
        return xscore_virtio_net_transmit(frame_buf, len);
    } else {
        process_transmitted_frame((const uint8_t *)buf, (uint32_t)len);
        return (int64_t)len;
    }
}

int64_t xscore_net_receive(void *buf, uint64_t max_len) {
    if (!buf || max_len == 0) return 0;
    if (xscore_virtio_net_present()) {
        xscore_virtio_net_poll_rx();
    }
    xscore_net_poll_loopback();
    if (rx_tail == rx_head) return 0; /* No packets */

    uint32_t copy_len = rx_ring[rx_tail].len;
    if (copy_len > max_len) copy_len = (uint32_t)max_len;

    memcpy(buf, rx_ring[rx_tail].data, copy_len);
    rx_tail = (rx_tail + 1) % NET_RING_SLOTS;

    return (int64_t)copy_len;
}

int xscore_net_poll_readable(void) {
    if (xscore_virtio_net_present()) {
        xscore_virtio_net_poll_rx();
    }
    xscore_net_poll_loopback();
    return rx_tail != rx_head;
}

int xscore_net_poll_writable(void) {
    return 1;
}

void xscore_net_get_stats(struct akaos_net_stats *stats) {
    if (stats) {
        *stats = net_stats;
    }
}

static void net_append_str(char *buf, size_t cap, size_t *used, const char *s) {
    if (!buf || !used || !s) return;
    while (*s && *used + 1 < cap) {
        buf[(*used)++] = *s++;
    }
    buf[*used < cap ? *used : cap - 1] = '\0';
}

static void net_append_u64(char *buf, size_t cap, size_t *used, uint64_t val) {
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

size_t xscore_net_format_proc_route(char *buf, size_t capacity) {
    if (!buf || capacity == 0) return 0;
    size_t used = 0;
    net_append_str(buf, capacity, &used,
        "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
    net_append_str(buf, capacity, &used,
        "eth0\t0002000A\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n");
    net_append_str(buf, capacity, &used,
        "eth0\t00000000\t0202000A\t0003\t0\t0\t0\t00000000\t0\t0\t0\n");
    net_append_str(buf, capacity, &used,
        "lo\t0000007F\t00000000\t0001\t0\t0\t0\t000000FF\t0\t0\t0\n");
    return used;
}

size_t xscore_net_format_proc_arp(char *buf, size_t capacity) {
    if (!buf || capacity == 0) return 0;
    size_t used = 0;
    net_append_str(buf, capacity, &used,
        "IP address       HW type     Flags       HW address            Mask     Device\n");
    net_append_str(buf, capacity, &used,
        "10.0.2.2         0x1         0x2         52:54:00:12:34:56     *        eth0\n");
    net_append_str(buf, capacity, &used,
        "10.0.2.3         0x1         0x2         52:54:00:12:34:56     *        eth0\n");
    return used;
}

size_t xscore_net_format_proc_dev(char *buf, size_t capacity) {
    if (!buf || capacity == 0) return 0;
    size_t used = 0;
    net_append_str(buf, capacity, &used,
        "Inter-|   Receive                                                |  Transmit\n");
    net_append_str(buf, capacity, &used,
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");

    if (xscore_virtio_net_present()) {
        net_append_str(buf, capacity, &used, "  eth0: ");
        net_append_u64(buf, capacity, &used, net_stats.rx_bytes);
        net_append_str(buf, capacity, &used, " ");
        net_append_u64(buf, capacity, &used, net_stats.rx_packets);
        net_append_str(buf, capacity, &used, "    0    0    0     0          0         0 ");
        net_append_u64(buf, capacity, &used, net_stats.tx_bytes);
        net_append_str(buf, capacity, &used, " ");
        net_append_u64(buf, capacity, &used, net_stats.tx_packets);
        net_append_str(buf, capacity, &used, "    0    0    0     0       0          0\n");
    }

    net_append_str(buf, capacity, &used,
        "    lo:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n");

    return used;
}
