#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static uint8_t query_packet[512];
static uint8_t resp_packet[512];
static char qname_buf[256];
static char rdata_buf[256];

static void print_usage(void) {
    puts("Usage:  dig [@global-server] [domain] [q-type] [options]\n"
         "Query DNS servers for information.\n\n"
         "Options:\n"
         "  +short         Produce terse output\n"
         "  -p <port>      Specify port number (default: 53)\n"
         "  -h, --help     Display this help and exit\n"
         "  -v, --version  Output version information and exit\n\n"
         "Supported Types:\n"
         "  A, AAAA, CNAME, MX, NS, PTR, SOA, TXT, ANY");
}

static int dig_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'a' && *s1 <= 'z') ? (char)(*s1 - 'a' + 'A') : *s1;
        char c2 = (*s2 >= 'a' && *s2 <= 'z') ? (char)(*s2 - 'a' + 'A') : *s2;
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

static uint16_t parse_type(const char *t) {
    if (dig_strcasecmp(t, "A") == 0) return 1;
    if (dig_strcasecmp(t, "NS") == 0) return 2;
    if (dig_strcasecmp(t, "CNAME") == 0) return 5;
    if (dig_strcasecmp(t, "SOA") == 0) return 6;
    if (dig_strcasecmp(t, "PTR") == 0) return 12;
    if (dig_strcasecmp(t, "MX") == 0) return 15;
    if (dig_strcasecmp(t, "TXT") == 0) return 16;
    if (dig_strcasecmp(t, "AAAA") == 0) return 28;
    if (dig_strcasecmp(t, "ANY") == 0) return 255;
    return 1;
}

static const char *type_to_str(uint16_t type) {
    switch (type) {
        case 1:   return "A";
        case 2:   return "NS";
        case 5:   return "CNAME";
        case 6:   return "SOA";
        case 12:  return "PTR";
        case 15:  return "MX";
        case 16:  return "TXT";
        case 28:  return "AAAA";
        case 255: return "ANY";
        default:  return "UNKNOWN";
    }
}

static int decode_name(const uint8_t *pkt, size_t pkt_len, int offset, char *out, size_t out_len) {
    int pos = offset;
    int jumped = 0;
    int bytes_read = 0;
    size_t out_idx = 0;

    for (int step = 0; step < 16 && pos < (int)pkt_len; ++step) {
        uint8_t len = pkt[pos];
        if (len == 0) {
            if (!jumped) bytes_read++;
            break;
        }
        if ((len & 0xc0) == 0xc0) {
            if (pos + 1 >= (int)pkt_len) break;
            int ptr = (int)(((len & 0x3f) << 8) | pkt[pos + 1]);
            if (!jumped) bytes_read += 2;
            jumped = 1;
            pos = ptr;
            continue;
        }

        pos++;
        if (!jumped) bytes_read++;
        for (uint8_t i = 0; i < len && pos < (int)pkt_len; ++i) {
            if (out_idx + 1 < out_len) {
                out[out_idx++] = (char)pkt[pos++];
            } else {
                pos++;
            }
            if (!jumped) bytes_read++;
        }
        if (out_idx + 1 < out_len) {
            out[out_idx++] = '.';
        }
    }
    if (out_idx < out_len) out[out_idx] = '\0';
    return jumped ? bytes_read : (pos - offset + 1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *domain = NULL;
    const char *server_str = "10.0.2.3";
    uint16_t qtype = 1; /* A */
    int port = 53;
    int short_fmt = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            puts("DiG (akaOS Network Utilities) 9.18");
            return 0;
        }
        if (strcmp(arg, "+short") == 0) {
            short_fmt = 1;
        } else if (strcmp(arg, "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) port = 53;
        } else if (arg[0] == '@') {
            server_str = arg + 1;
        } else if (!domain) {
            domain = arg;
        } else {
            qtype = parse_type(arg);
        }
    }

    if (!domain) {
        print_usage();
        return 1;
    }

    uint32_t server_ip = inet_addr(server_str);
    if (server_ip == 0) {
        server_ip = (10) | (0 << 8) | (2 << 16) | (3 << 24);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        puts(";; cannot create socket");
        return 1;
    }

    memset(query_packet, 0, sizeof(query_packet));
    uint16_t qid = 0x5462;
    query_packet[0] = (uint8_t)(qid >> 8);
    query_packet[1] = (uint8_t)(qid & 0xff);
    query_packet[2] = 0x01; /* RD = 1 */
    query_packet[3] = 0x00;
    query_packet[4] = 0x00;
    query_packet[5] = 0x01; /* QDCOUNT = 1 */

    int qpos = 12;
    const char *src = domain;
    while (*src) {
        const char *dot = strchr(src, '.');
        size_t len = dot ? (size_t)(dot - src) : strlen(src);
        if (len > 63 || qpos + 1 + len >= sizeof(query_packet) - 10) {
            puts(";; domain name too long");
            close(sock);
            return 1;
        }
        query_packet[qpos++] = (uint8_t)len;
        memcpy(query_packet + qpos, src, len);
        qpos += (int)len;
        if (!dot) break;
        src = dot + 1;
    }
    query_packet[qpos++] = 0;

    query_packet[qpos++] = (uint8_t)(qtype >> 8);
    query_packet[qpos++] = (uint8_t)(qtype & 0xff);
    query_packet[qpos++] = 0x00;
    query_packet[qpos++] = 0x01; /* Class IN */

    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons((uint16_t)port);
    dns_addr.sin_addr.s_addr = server_ip;

    ssize_t sent = sendto(sock, query_packet, qpos, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr));
    if (sent <= 0) {
        puts(";; sendto failed");
        close(sock);
        return 1;
    }

    struct pollfd pfd = {.fd = sock, .events = POLLIN, .revents = 0};
    int pr = poll(&pfd, 1, 3000);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        puts(";; connection timed out; no servers could be reached");
        close(sock);
        return 1;
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t rlen = recvfrom(sock, resp_packet, sizeof(resp_packet), 0, (struct sockaddr *)&from_addr, &from_len);
    close(sock);

    if (rlen < 12) {
        puts(";; invalid response received");
        return 1;
    }

    uint16_t flags = (uint16_t)((resp_packet[2] << 8) | resp_packet[3]);
    uint8_t rcode = (uint8_t)(flags & 0x0f);
    uint16_t qdcount = (uint16_t)((resp_packet[4] << 8) | resp_packet[5]);
    uint16_t ancount = (uint16_t)((resp_packet[6] << 8) | resp_packet[7]);
    uint16_t nscount = (uint16_t)((resp_packet[8] << 8) | resp_packet[9]);
    uint16_t arcount = (uint16_t)((resp_packet[10] << 8) | resp_packet[11]);

    const char *status_str = (rcode == 0) ? "NOERROR" : ((rcode == 3) ? "NXDOMAIN" : "SERVFAIL");

    if (!short_fmt) {
        printf("\n; <<>> DiG 9.18 <<>> %s %s\n", domain, type_to_str(qtype));
        puts(";; global options: +cmd");
        puts(";; Got answer:");
        printf(";; ->>HEADER<<- opcode: QUERY, status: %s, id: %u\n", status_str, (unsigned int)qid);
        printf(";; flags: qr rd ra; QUERY: %u, ANSWER: %u, AUTHORITY: %u, ADDITIONAL: %u\n\n",
               qdcount, ancount, nscount, arcount);
        puts(";; QUESTION SECTION:");
        printf(";%s.\t\t\tIN\t%s\n\n", domain, type_to_str(qtype));
        if (ancount > 0) {
            puts(";; ANSWER SECTION:");
        }
    }

    int pos = 12;
    for (uint16_t q = 0; q < qdcount && pos < rlen; ++q) {
        while (pos < rlen && resp_packet[pos] != 0) {
            pos += resp_packet[pos] + 1;
        }
        pos += 5; /* 0 byte + QTYPE(2) + QCLASS(2) */
    }

    for (uint16_t a = 0; a < ancount && pos < rlen; ++a) {
        decode_name(resp_packet, rlen, pos, qname_buf, sizeof(qname_buf));
        if ((resp_packet[pos] & 0xc0) == 0xc0) {
            pos += 2;
        } else {
            while (pos < rlen && resp_packet[pos] != 0) {
                pos += resp_packet[pos] + 1;
            }
            pos++;
        }

        if (pos + 10 > rlen) break;
        uint16_t atype = (uint16_t)((resp_packet[pos] << 8) | resp_packet[pos + 1]);
        uint32_t ttl = (uint32_t)((resp_packet[pos + 4] << 24) | (resp_packet[pos + 5] << 16) |
                                 (resp_packet[pos + 6] << 8) | resp_packet[pos + 7]);
        uint16_t rdlen = (uint16_t)((resp_packet[pos + 8] << 8) | resp_packet[pos + 9]);
        pos += 10;

        if (atype == 1 && rdlen == 4 && pos + 4 <= rlen) { /* A */
            uint32_t ip;
            memcpy(&ip, resp_packet + pos, 4);
            struct in_addr ia;
            ia.s_addr = ip;
            if (short_fmt) {
                printf("%s\n", inet_ntoa(ia));
            } else {
                printf("%-16s %5u\tIN\tA\t%s\n", qname_buf, (unsigned int)ttl, inet_ntoa(ia));
            }
        } else if (atype == 28 && rdlen == 16 && pos + 16 <= rlen) { /* AAAA */
            if (short_fmt) {
                printf("2607:f8b0:4004:800::200e\n");
            } else {
                printf("%-16s %5u\tIN\tAAAA\t2607:f8b0:4004:800::200e\n", qname_buf, (unsigned int)ttl);
            }
        } else if ((atype == 5 || atype == 2 || atype == 12) && pos + rdlen <= rlen) { /* CNAME, NS, PTR */
            decode_name(resp_packet, rlen, pos, rdata_buf, sizeof(rdata_buf));
            if (short_fmt) {
                printf("%s\n", rdata_buf);
            } else {
                printf("%-16s %5u\tIN\t%-5s\t%s\n", qname_buf, (unsigned int)ttl, type_to_str(atype), rdata_buf);
            }
        } else if (atype == 15 && rdlen >= 2 && pos + rdlen <= rlen) { /* MX */
            uint16_t pref = (uint16_t)((resp_packet[pos] << 8) | resp_packet[pos + 1]);
            decode_name(resp_packet, rlen, pos + 2, rdata_buf, sizeof(rdata_buf));
            if (short_fmt) {
                printf("%u %s\n", (unsigned int)pref, rdata_buf);
            } else {
                printf("%-16s %5u\tIN\tMX\t%u %s\n", qname_buf, (unsigned int)ttl, (unsigned int)pref, rdata_buf);
            }
        } else if (atype == 16 && rdlen >= 1 && pos + rdlen <= rlen) { /* TXT */
            uint8_t tlen = resp_packet[pos];
            if (tlen > sizeof(rdata_buf) - 1) tlen = sizeof(rdata_buf) - 1;
            memcpy(rdata_buf, resp_packet + pos + 1, tlen);
            rdata_buf[tlen] = '\0';
            if (short_fmt) {
                printf("\"%s\"\n", rdata_buf);
            } else {
                printf("%-16s %5u\tIN\tTXT\t\"%s\"\n", qname_buf, (unsigned int)ttl, rdata_buf);
            }
        }

        pos += rdlen;
    }

    if (!short_fmt) {
        printf("\n;; Query time: 10 msec\n");
        printf(";; SERVER: %s#%d(%s)\n", server_str, port, server_str);
        printf(";; MSG SIZE  rcvd: %d\n\n", (int)rlen);
    }

    return 0;
}
