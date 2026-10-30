#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TFTP_OP_RRQ   1
#define TFTP_OP_WRQ   2
#define TFTP_OP_DATA  3
#define TFTP_OP_ACK   4
#define TFTP_OP_ERROR 5

static void print_usage(void) {
    printf("Usage:\n"
           "  tftp get <host> <remote-file> [local-file]\n"
           "  tftp put <host> <local-file> [remote-file]\n");
}

static uint32_t resolve_host(const char *name) {
    uint32_t addr = inet_addr(name);
    if (addr != 0) return addr;
    struct hostent *he = gethostbyname(name);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        return *(uint32_t *)he->h_addr_list[0];
    }
    return 0;
}

static int do_get(const char *host, const char *remotefile, const char *localfile, int port) {
    uint32_t ip = resolve_host(host);
    if (!ip) {
        fprintf(stderr, "tftp: unable to resolve host %s\n", host);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("tftp: socket failed");
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = ip;
    srv.sin_port = htons((uint16_t)port);

    /* Construct RRQ */
    uint8_t pkt[512];
    pkt[0] = 0; pkt[1] = TFTP_OP_RRQ;
    size_t flen = strlen(remotefile);
    memcpy(pkt + 2, remotefile, flen + 1);
    const char *mode = "octet";
    size_t mlen = strlen(mode);
    memcpy(pkt + 2 + flen + 1, mode, mlen + 1);
    size_t req_len = 2 + flen + 1 + mlen + 1;

    sendto(sockfd, pkt, req_len, 0, (struct sockaddr *)&srv, sizeof(srv));

    int out_fd = open(localfile, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        perror("tftp: cannot create local file");
        close(sockfd);
        return 1;
    }

    uint16_t expected_block = 1;
    uint8_t recv_buf[516];
    size_t total_bytes = 0;

    while (1) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 3000) <= 0) {
            fprintf(stderr, "tftp: transfer timed out\n");
            break;
        }

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&from, &flen);
        if (n >= 4) {
            uint16_t op = (uint16_t)((recv_buf[0] << 8) | recv_buf[1]);
            if (op == TFTP_OP_DATA) {
                uint16_t blk = (uint16_t)((recv_buf[2] << 8) | recv_buf[3]);
                if (blk == expected_block) {
                    if (n > 4) {
                        write(out_fd, recv_buf + 4, (size_t)(n - 4));
                        total_bytes += (size_t)(n - 4);
                    }
                    /* Send ACK */
                    uint8_t ack[4] = {0, TFTP_OP_ACK, recv_buf[2], recv_buf[3]};
                    sendto(sockfd, ack, 4, 0, (struct sockaddr *)&from, flen);
                    expected_block++;
                    if (n < 516) {
                        printf("Received %u bytes\n", (unsigned)total_bytes);
                        close(out_fd);
                        close(sockfd);
                        return 0;
                    }
                }
            } else if (op == TFTP_OP_ERROR) {
                recv_buf[n] = '\0';
                fprintf(stderr, "tftp: server error: %s\n", (char *)(recv_buf + 4));
                break;
            }
        }
    }

    close(out_fd);
    close(sockfd);
    return 1;
}

static int do_put(const char *host, const char *localfile, const char *remotefile, int port) {
    uint32_t ip = resolve_host(host);
    if (!ip) {
        fprintf(stderr, "tftp: unable to resolve host %s\n", host);
        return 1;
    }

    int in_fd = open(localfile, O_RDONLY);
    if (in_fd < 0) {
        perror("tftp: cannot open local file");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        close(in_fd);
        perror("tftp: socket failed");
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = ip;
    srv.sin_port = htons((uint16_t)port);

    /* Construct WRQ */
    uint8_t pkt[516];
    pkt[0] = 0; pkt[1] = TFTP_OP_WRQ;
    size_t flen = strlen(remotefile);
    memcpy(pkt + 2, remotefile, flen + 1);
    const char *mode = "octet";
    size_t mlen = strlen(mode);
    memcpy(pkt + 2 + flen + 1, mode, mlen + 1);
    size_t req_len = 2 + flen + 1 + mlen + 1;

    sendto(sockfd, pkt, req_len, 0, (struct sockaddr *)&srv, sizeof(srv));

    /* Wait for ACK block 0 */
    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 3000) <= 0) {
        fprintf(stderr, "tftp: server didn't respond to WRQ\n");
        close(in_fd);
        close(sockfd);
        return 1;
    }

    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t rn = recvfrom(sockfd, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fromlen);
    if (rn < 4 || pkt[1] != TFTP_OP_ACK) {
        fprintf(stderr, "tftp: invalid initial ACK\n");
        close(in_fd);
        close(sockfd);
        return 1;
    }

    uint16_t block = 1;
    size_t total_sent = 0;

    while (1) {
        ssize_t n = read(in_fd, pkt + 4, 512);
        if (n < 0) n = 0;

        pkt[0] = 0; pkt[1] = TFTP_OP_DATA;
        pkt[2] = (uint8_t)(block >> 8);
        pkt[3] = (uint8_t)(block & 0xff);

        sendto(sockfd, pkt, (size_t)(4 + n), 0, (struct sockaddr *)&from, fromlen);
        total_sent += (size_t)n;

        pfd.fd = sockfd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 3000) <= 0) {
            fprintf(stderr, "tftp: timeout waiting for ACK %d\n", block);
            break;
        }

        rn = recvfrom(sockfd, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fromlen);
        if (rn >= 4 && pkt[1] == TFTP_OP_ACK) {
            uint16_t ack = (uint16_t)((pkt[2] << 8) | pkt[3]);
            if (ack == block) {
                block++;
                if (n < 512) {
                    printf("Sent %u bytes\n", (unsigned)total_sent);
                    close(in_fd);
                    close(sockfd);
                    return 0;
                }
                continue;
            }
        }
        break;
    }

    close(in_fd);
    close(sockfd);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];
    const char *host = argv[2];
    const char *file1 = argv[3];
    const char *file2 = (argc >= 5) ? argv[4] : file1;
    int port = 69;

    if (strcmp(cmd, "get") == 0) {
        return do_get(host, file1, file2, port);
    } else if (strcmp(cmd, "put") == 0) {
        return do_put(host, file1, file2, port);
    }

    print_usage();
    return 1;
}
