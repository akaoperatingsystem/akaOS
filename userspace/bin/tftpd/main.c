#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TFTP_OP_RRQ   1
#define TFTP_OP_WRQ   2
#define TFTP_OP_DATA  3
#define TFTP_OP_ACK   4
#define TFTP_OP_ERROR 5

static int parse_config_int(const char *key, int default_val) {
    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd < 0) return default_val;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return default_val;
    buf[n] = '\0';

    char *line = buf;
    size_t klen = strlen(key);
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            return atoi(line + klen + 1);
        }
        if (!eol) break;
        line = eol + 1;
    }
    return default_val;
}

static void send_error(int sockfd, struct sockaddr_in *client, uint16_t code, const char *msg) {
    uint8_t pkt[512];
    pkt[0] = 0; pkt[1] = TFTP_OP_ERROR;
    pkt[2] = (uint8_t)(code >> 8); pkt[3] = (uint8_t)(code & 0xff);
    size_t mlen = strlen(msg);
    if (mlen > sizeof(pkt) - 5) mlen = sizeof(pkt) - 5;
    memcpy(pkt + 4, msg, mlen);
    pkt[4 + mlen] = '\0';
    sendto(sockfd, pkt, 5 + mlen, 0, (struct sockaddr *)client, sizeof(*client));
}

static void handle_rrq(int sockfd, struct sockaddr_in *client, const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        send_error(sockfd, client, 1, "File not found");
        return;
    }

    uint16_t block = 1;
    uint8_t send_buf[516];
    uint8_t recv_buf[516];

    while (1) {
        ssize_t n = read(fd, send_buf + 4, 512);
        if (n < 0) n = 0;

        send_buf[0] = 0; send_buf[1] = TFTP_OP_DATA;
        send_buf[2] = (uint8_t)(block >> 8);
        send_buf[3] = (uint8_t)(block & 0xff);

        /* Send DATA */
        sendto(sockfd, send_buf, (size_t)(4 + n), 0, (struct sockaddr *)client, sizeof(*client));

        /* Wait for ACK */
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 2000) <= 0) {
            break;
        }

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t rn = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&from, &flen);
        if (rn >= 4 && recv_buf[1] == TFTP_OP_ACK) {
            uint16_t ack_block = (uint16_t)((recv_buf[2] << 8) | recv_buf[3]);
            if (ack_block == block) {
                block++;
                if (n < 512) break; /* Finished */
                continue;
            }
        }
        break;
    }
    close(fd);
}

static void handle_wrq(int sockfd, struct sockaddr_in *client, const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        send_error(sockfd, client, 2, "Access violation or cannot create file");
        return;
    }

    /* Send initial ACK block 0 */
    uint8_t ack[4] = {0, TFTP_OP_ACK, 0, 0};
    sendto(sockfd, ack, sizeof(ack), 0, (struct sockaddr *)client, sizeof(*client));

    uint16_t expected_block = 1;
    uint8_t recv_buf[516];

    while (1) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 3000) <= 0) break;

        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&from, &flen);
        if (n >= 4 && recv_buf[1] == TFTP_OP_DATA) {
            uint16_t block = (uint16_t)((recv_buf[2] << 8) | recv_buf[3]);
            if (block == expected_block) {
                if (n > 4) {
                    write(fd, recv_buf + 4, (size_t)(n - 4));
                }
                ack[2] = recv_buf[2];
                ack[3] = recv_buf[3];
                sendto(sockfd, ack, sizeof(ack), 0, (struct sockaddr *)&from, flen);
                expected_block++;
                if (n < 516) break; /* Final short block */
            }
        }
    }
    close(fd);
}

int main(int argc, char **argv) {
    int port = parse_config_int("TFTPD_PORT", 69);
    int enabled = parse_config_int("TFTPD_ENABLED", 0);
    int force_run = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            force_run = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            force_run = 1;
        } else if (strcmp(argv[i], "--status") == 0) {
            printf("TFTPD is %s (port %d)\n", enabled ? "ENABLED" : "DISABLED", port);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: tftpd [-p port] [-f] [--status]\n");
            return 0;
        }
    }

    if (!enabled && !force_run) {
        printf("tftpd: disabled in /etc/akaos.conf (use akaos-config --enable-tftpd)\n");
        return 0;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("tftpd: failed to create socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("tftpd: bind failed");
        close(sockfd);
        return 1;
    }

    printf("[tftpd] TFTP daemon listening on UDP port %d\n", port);

    uint8_t buffer[1024];
    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client, &clen);
        if (n >= 4) {
            buffer[n] = '\0';
            uint16_t op = (uint16_t)((buffer[0] << 8) | buffer[1]);
            const char *filename = (const char *)(buffer + 2);
            if (op == TFTP_OP_RRQ) {
                handle_rrq(sockfd, &client, filename);
            } else if (op == TFTP_OP_WRQ) {
                handle_wrq(sockfd, &client, filename);
            }
        }
    }

    close(sockfd);
    return 0;
}
