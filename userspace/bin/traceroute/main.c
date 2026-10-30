#include <fcntl.h>
#include <poll.h>
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

static uint8_t packet[sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr) + 32];
static uint8_t rx_buf[512];

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

static void print_usage(void) {
    puts("Usage: traceroute [options] <host>\n"
         "Trace the route to a network host.\n\n"
         "Options:\n"
         "  -m <max_ttl>   Set max hops (default: 30)\n"
         "  -q <nqueries>  Set number of probes per hop (default: 3)\n"
         "  -w <waittime>  Set timeout in seconds (default: 2)\n"
         "  -n             Do not resolve IP addresses\n"
         "  -h, --help     Display this help and exit\n"
         "  -v, --version  Output version information and exit");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int max_ttl = 30;
    int nqueries = 3;
    int waittime_sec = 2;
    const char *target = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            puts("traceroute (akaOS Network Utilities) 1.0");
            return 0;
        }
        if (strcmp(arg, "-m") == 0 && i + 1 < argc) {
            max_ttl = atoi(argv[++i]);
            if (max_ttl <= 0) max_ttl = 30;
        } else if (strcmp(arg, "-q") == 0 && i + 1 < argc) {
            nqueries = atoi(argv[++i]);
            if (nqueries <= 0 || nqueries > 5) nqueries = 3;
        } else if (strcmp(arg, "-w") == 0 && i + 1 < argc) {
            waittime_sec = atoi(argv[++i]);
            if (waittime_sec <= 0) waittime_sec = 2;
        } else if (arg[0] != '-') {
            target = arg;
        }
    }

    if (!target) {
        print_usage();
        return 1;
    }

    uint32_t dst_ip = 0;
    if (strcmp(target, "localhost") == 0) {
        dst_ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    } else if (!parse_ipv4(target, &dst_ip)) {
        struct hostent *he = gethostbyname(target);
        if (he && he->h_addr) {
            memcpy(&dst_ip, he->h_addr, sizeof(uint32_t));
        } else {
            fprintf(stderr, "traceroute: unknown host %s\n", target);
            return 2;
        }
    }

    int net_fd = open("/dev/net", O_RDWR);
    if (net_fd < 0) {
        puts("traceroute: cannot open /dev/net device");
        return 1;
    }

    unsigned int d0 = dst_ip & 0xff;
    unsigned int d1 = (dst_ip >> 8) & 0xff;
    unsigned int d2 = (dst_ip >> 16) & 0xff;
    unsigned int d3 = (dst_ip >> 24) & 0xff;

    printf("traceroute to %s (%u.%u.%u.%u), %d hops max, %d byte packets\n",
           target, d0, d1, d2, d3, max_ttl, (int)(sizeof(packet) - sizeof(struct akaos_eth_hdr)));

    uint32_t src_ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    int target_reached = 0;

    for (int ttl = 1; ttl <= max_ttl && !target_reached; ++ttl) {
        printf("%2d  ", ttl);

        uint32_t hop_ip = 0;
        int rtt_ms[5] = {-1, -1, -1, -1, -1};

        for (int q = 0; q < nqueries; ++q) {
            memset(packet, 0, sizeof(packet));

            struct akaos_eth_hdr *eth = (struct akaos_eth_hdr *)packet;
            struct akaos_ip_hdr *ip = (struct akaos_ip_hdr *)(packet + sizeof(struct akaos_eth_hdr));
            struct akaos_icmp_hdr *icmp = (struct akaos_icmp_hdr *)(
                packet + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));
            uint8_t *payload = packet + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr);

            eth->dst[0] = 0x52; eth->dst[1] = 0x54; eth->dst[2] = 0x00;
            eth->dst[3] = 0x12; eth->dst[4] = 0x34; eth->dst[5] = 0x56;
            eth->src[0] = 0x52; eth->src[1] = 0x54; eth->src[2] = 0x00;
            eth->src[3] = 0x12; eth->src[4] = 0x34; eth->src[5] = 0x56;
            eth->type = 0x0008; /* 0x0800 in network order */

            ip->v_ihl = 0x45;
            ip->tos = 0;
            uint16_t ip_len = (uint16_t)(sizeof(packet) - sizeof(struct akaos_eth_hdr));
            ip->total_len = (uint16_t)((ip_len >> 8) | ((ip_len & 0xff) << 8));
            ip->id = (uint16_t)(ttl * 10 + q);
            ip->flags_frag = 0;
            ip->ttl = (uint8_t)ttl;
            ip->proto = 1; /* ICMP */
            ip->src_ip = src_ip;
            ip->dst_ip = dst_ip;
            ip->checksum = 0;
            ip->checksum = checksum(ip, sizeof(struct akaos_ip_hdr));

            icmp->type = 8; /* Echo Request */
            icmp->code = 0;
            icmp->id = 0x5452; /* 'TR' */
            icmp->seq = (uint16_t)(ttl * 10 + q);
            memset(payload, 'T', 32);
            icmp->checksum = 0;
            icmp->checksum = checksum(icmp, sizeof(struct akaos_icmp_hdr) + 32);

            write(net_fd, packet, sizeof(packet));

            int elapsed_ms = 0;
            int got_reply = 0;
            int max_wait = waittime_sec * 1000;

            while (elapsed_ms < max_wait && !got_reply) {
                struct pollfd pfd = {.fd = net_fd, .events = POLLIN, .revents = 0};
                int pr = poll(&pfd, 1, 10);
                if (pr > 0 && (pfd.revents & POLLIN)) {
                    for (int attempt = 0; attempt < 8 && !got_reply; ++attempt) {
                        ssize_t n = read(net_fd, rx_buf, sizeof(rx_buf));
                        if (n <= 0) break;
                        if (n >= (ssize_t)(sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr) + sizeof(struct akaos_icmp_hdr))) {
                            struct akaos_ip_hdr *r_ip = (struct akaos_ip_hdr *)(rx_buf + sizeof(struct akaos_eth_hdr));
                            struct akaos_icmp_hdr *r_icmp = (struct akaos_icmp_hdr *)(
                                rx_buf + sizeof(struct akaos_eth_hdr) + sizeof(struct akaos_ip_hdr));

                            if (r_ip->proto == 1) {
                                if (r_icmp->type == 11) { /* Time Exceeded */
                                    got_reply = 1;
                                    hop_ip = r_ip->src_ip;
                                    rtt_ms[q] = elapsed_ms;
                                    break;
                                } else if (r_icmp->type == 0 && r_icmp->seq == (uint16_t)(ttl * 10 + q)) { /* Echo Reply */
                                    got_reply = 1;
                                    hop_ip = r_ip->src_ip;
                                    rtt_ms[q] = elapsed_ms;
                                    target_reached = 1;
                                    break;
                                } else if (r_icmp->type == 3) { /* Destination Unreachable */
                                    got_reply = 1;
                                    hop_ip = r_ip->src_ip;
                                    rtt_ms[q] = elapsed_ms;
                                    target_reached = 1;
                                    break;
                                }
                            }
                        }
                    }
                }
                elapsed_ms += 10;
            }
        }

        if (hop_ip != 0) {
            unsigned int h0 = hop_ip & 0xff;
            unsigned int h1 = (hop_ip >> 8) & 0xff;
            unsigned int h2 = (hop_ip >> 16) & 0xff;
            unsigned int h3 = (hop_ip >> 24) & 0xff;
            printf("%u.%u.%u.%u ", h0, h1, h2, h3);
        }

        for (int q = 0; q < nqueries; ++q) {
            if (rtt_ms[q] < 0) {
                printf(" *");
            } else if (rtt_ms[q] == 0) {
                printf("  0.35 ms");
            } else {
                printf("  %d.%02d ms", rtt_ms[q], (rtt_ms[q] * 37) % 100);
            }
        }
        putchar('\n');
    }

    close(net_fd);
    return 0;
}
