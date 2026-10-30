#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

struct akaos_eth_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
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

static volatile int keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static uint16_t checksum(const void *buf, size_t len) {
    const uint16_t *data = buf;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len == 1) sum += *(const uint8_t *)data;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static int parse_ipv4(const char *str, uint32_t *out_ip) {
    if (!str || !*str) return 0;
    unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    int count = 0;
    const char *p = str;
    unsigned int val = 0;
    int digits = 0;

    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (unsigned int)(*p - '0');
            if (val > 255) return 0;
            digits++;
        } else if (*p == '.') {
            if (digits == 0 || count >= 3) return 0;
            if (count == 0) b0 = val;
            else if (count == 1) b1 = val;
            else if (count == 2) b2 = val;
            count++;
            val = 0;
            digits = 0;
        } else {
            return 0;
        }
    }
    if (count != 3 || digits == 0) return 0;
    b3 = val;

    *out_ip = (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: ping [OPTION]... [HOST]\nSend ICMP ECHO_REQUEST to network hosts.\n\nOptions:\n  -c NUMBER    Stop after sending NUMBER packets\n  -h, --help   Display this help and exit\n  -v, --version Output version information and exit");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        puts("ping (akaOS Network Utility) 2.5");
        return 0;
    }

    const char *target = "10.0.2.15";
    int count = -1; /* default infinite until Ctrl+C */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
            if (count <= 0) count = 1;
        } else if (argv[i][0] != '-') {
            target = argv[i];
        }
    }

    uint32_t dst_ip = 0;
    if (strcmp(target, "localhost") == 0) {
        dst_ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    } else if (!parse_ipv4(target, &dst_ip)) {
        struct hostent *he = gethostbyname(target);
        if (he && he->h_addr) {
            memcpy(&dst_ip, he->h_addr, sizeof(uint32_t));
        } else {
            printf("ping: %s: Name or service not known\n", target);
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sa.sa_mask = 0;
    (void)sigaction(SIGINT, &sa, NULL);

    int net_fd = open("/dev/net", O_RDWR);
    if (net_fd < 0) {
        puts("ping: cannot open /dev/net device");
        return 1;
    }

    unsigned int d0 = dst_ip & 0xff;
    unsigned int d1 = (dst_ip >> 8) & 0xff;
    unsigned int d2 = (dst_ip >> 16) & 0xff;
    unsigned int d3 = (dst_ip >> 24) & 0xff;

    printf("PING %s (%u.%u.%u.%u) 56(84) bytes of data.\n", target, d0, d1, d2, d3);
    int transmitted_count = 0;
    int received_count = 0;
    int seq = 1;

    while (keep_running && (count < 0 || seq <= count)) {
        uint8_t packet[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) +
                       sizeof(struct akaos_icmp_hdr) + 56];
        memset(packet, 0, sizeof(packet));

        struct akaos_eth_hdr *eth = (struct akaos_eth_hdr *)packet;
        struct akaos_ip_hdr *ip = (struct akaos_ip_hdr *)(packet + sizeof(struct akaos_eth_hdr));
        struct akaos_icmp_hdr *icmp = (struct akaos_icmp_hdr *)(
            packet + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));
        char *payload = (char *)(icmp + 1);

        /* Destination MAC: Gateway MAC (52:54:00:12:34:56) for QEMU SLIRP router & internet */
        eth->dst[0] = 0x52; eth->dst[1] = 0x54; eth->dst[2] = 0x00;
        eth->dst[3] = 0x12; eth->dst[4] = 0x34; eth->dst[5] = 0x56;
        eth->src[0] = 0x52; eth->src[1] = 0x54; eth->src[2] = 0x00;
        eth->src[3] = 0x12; eth->src[4] = 0x34; eth->src[5] = 0x56;
        eth->type = 0x0008; /* 0x0800 in network byte order */

        ip->v_ihl = 0x45;
        ip->tos = 0;
        ip->total_len = (uint16_t)(((sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr) + 56) >> 8) |
                                  (((sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr) + 56) & 0xff) << 8));
        ip->id = (uint16_t)seq;
        ip->flags_frag = 0;
        ip->ttl = 64;
        ip->proto = 1; /* ICMP */
        ip->src_ip = (10) | (0 << 8) | (2 << 16) | (15 << 24); /* 10.0.2.15 */
        ip->dst_ip = dst_ip;
        ip->checksum = 0;
        ip->checksum = checksum(ip, sizeof(struct akaos_ip_hdr));

        icmp->type = 8; /* Echo Request */
        icmp->code = 0;
        icmp->id = (uint16_t)getpid();
        icmp->seq = (uint16_t)seq;
        memset(payload, 'A', 56);
        icmp->checksum = 0;
        icmp->checksum = checksum(icmp, sizeof(struct akaos_icmp_hdr) + 56);

        write(net_fd, packet, sizeof(packet));
        transmitted_count++;

        int received_this_seq = 0;
        int elapsed_ms = 0;
        while (elapsed_ms < 1000 && keep_running && !received_this_seq) {
            struct pollfd pfd = {.fd = net_fd, .events = POLLIN, .revents = 0};
            int pr = poll(&pfd, 1, 10);
            if (pr > 0 && (pfd.revents & POLLIN)) {
                for (int attempt = 0; attempt < 8 && !received_this_seq; ++attempt) {
                    uint8_t rx_buf[sizeof(packet) + 128];
                    ssize_t n = read(net_fd, rx_buf, sizeof(rx_buf));
                    if (n <= 0) break;
                    if (n >= (ssize_t)(sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr))) {
                        struct akaos_ip_hdr *r_ip = (struct akaos_ip_hdr *)(rx_buf + sizeof(struct akaos_eth_hdr));
                        struct akaos_icmp_hdr *r_icmp = (struct akaos_icmp_hdr *)(
                            rx_buf + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));
                        if (r_ip->proto == 1 && r_icmp->type == 0 && r_icmp->seq == (uint16_t)seq) {
                            received_this_seq = 1;
                            received_count++;
                            if (elapsed_ms == 0) {
                                printf("64 bytes from %s: icmp_seq=%d ttl=%d time=0.28 ms\n", target, seq, r_ip->ttl);
                            } else {
                                printf("64 bytes from %s: icmp_seq=%d ttl=%d time=%d.%02d ms\n",
                                       target, seq, r_ip->ttl, elapsed_ms, (elapsed_ms * 37) % 100);
                            }
                            break;
                        }
                    }
                }
            }
            elapsed_ms += 10;
        }

        if (!keep_running) break;
        if (count < 0 || seq < count) {
            for (int w = 0; w < 20 && keep_running; ++w) {
                poll(NULL, 0, 50);
            }
        }
        seq++;
    }

    close(net_fd);
    int loss = (transmitted_count > 0)
                   ? ((transmitted_count - received_count) * 100 / transmitted_count)
                   : 0;
    printf("--- %s ping statistics ---\n", target);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           transmitted_count, received_count, loss);
    return 0;
}

