#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static void print_usage(void) {
    puts("Usage: nslookup [OPTION] <host> [server]\n"
         "Query Internet name servers interactively or non-interactively.\n\n"
         "Options:\n"
         "  -h, --help     Display this help and exit\n"
         "  -v, --version  Output version information and exit");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        puts("nslookup (akaOS Network Utility) 1.0");
        return 0;
    }

    const char *domain = argv[1];
    const char *server_str = (argc >= 3) ? argv[2] : "10.0.2.3";

    uint32_t server_ip = inet_addr(server_str);
    if (server_ip == 0) {
        server_ip = (10) | (0 << 8) | (2 << 16) | (3 << 24);
    }

    printf("Server:\t\t%s\n", server_str);
    printf("Address:\t%s#53\n\n", server_str);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        puts("nslookup: cannot create socket");
        return 1;
    }

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    packet[0] = 0x24; packet[1] = 0x68; /* ID */
    packet[2] = 0x01; packet[3] = 0x00; /* RD=1 */
    packet[4] = 0x00; packet[5] = 0x01; /* QDCOUNT=1 */

    int qpos = 12;
    const char *src = domain;
    while (*src) {
        const char *dot = strchr(src, '.');
        size_t len = dot ? (size_t)(dot - src) : strlen(src);
        if (len > 63 || qpos + 1 + len >= sizeof(packet) - 10) {
            puts("nslookup: domain name too long");
            close(sock);
            return 1;
        }
        packet[qpos++] = (uint8_t)len;
        memcpy(packet + qpos, src, len);
        qpos += (int)len;
        if (!dot) break;
        src = dot + 1;
    }
    packet[qpos++] = 0;

    packet[qpos++] = 0x00; packet[qpos++] = 0x01; /* QTYPE=A */
    packet[qpos++] = 0x00; packet[qpos++] = 0x01; /* QCLASS=IN */

    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);
    dns_addr.sin_addr.s_addr = server_ip;

    ssize_t sent = sendto(sock, packet, qpos, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr));
    if (sent <= 0) {
        puts(";; sendto failed");
        close(sock);
        return 1;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, 3000);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        puts(";; connection timed out; no servers could be reached");
        close(sock);
        return 1;
    }

    uint8_t resp[512];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t rlen = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&from_addr, &from_len);
    close(sock);

    if (rlen < 12) {
        puts(";; invalid response received");
        return 1;
    }

    uint16_t flags = (uint16_t)((resp[2] << 8) | resp[3]);
    uint8_t rcode = (uint8_t)(flags & 0x0f);
    if (rcode != 0) {
        printf("** server can't find %s: NXDOMAIN\n", domain);
        return 1;
    }

    uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    if (ancount == 0) {
        printf("** server can't find %s: No answer\n", domain);
        return 1;
    }

    int pos = 12;
    while (pos < rlen && resp[pos] != 0) {
        pos += resp[pos] + 1;
    }
    pos += 5;

    puts("Non-authoritative answer:");
    printf("Name:\t%s\n", domain);

    int found = 0;
    for (uint16_t a = 0; a < ancount && pos < rlen; ++a) {
        if ((resp[pos] & 0xc0) == 0xc0) {
            pos += 2;
        } else {
            while (pos < rlen && resp[pos] != 0) {
                pos += resp[pos] + 1;
            }
            pos++;
        }
        if (pos + 10 > rlen) break;
        uint16_t atype = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
        uint16_t rdlen = (uint16_t)((resp[pos + 8] << 8) | resp[pos + 9]);
        pos += 10;
        if (atype == 1 && rdlen == 4 && pos + 4 <= rlen) {
            uint32_t ip;
            memcpy(&ip, resp + pos, 4);
            struct in_addr ia;
            ia.s_addr = ip;
            printf("Address: %s\n", inet_ntoa(ia));
            found = 1;
        }
        pos += rdlen;
    }

    if (!found) {
        printf("** server can't find %s: No A record found\n", domain);
        return 1;
    }

    return 0;
}
