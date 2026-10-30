#include <akaos/account.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#define SSH_CLIENT_BANNER "SSH-2.0-akaOS_SSH_1.0\r\n"

static struct termios g_saved_termios;
static int g_termios_saved = 0;

static void restore_terminal(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_termios_saved = 0;
    }
}

static void on_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(1);
}

static int resolve_ip(const char *hostname, struct in_addr *out_addr) {
    if (!hostname || !out_addr) return 0;
    if (strcmp(hostname, "localhost") == 0 || strcmp(hostname, "akaOS") == 0) {
        out_addr->s_addr = inet_addr("127.0.0.1");
        return 1;
    }
    uint32_t addr = inet_addr(hostname);
    if (addr != (uint32_t)(-1)) {
        out_addr->s_addr = addr;
        return 1;
    }
    struct hostent *he = gethostbyname(hostname);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        memcpy(out_addr, he->h_addr_list[0], sizeof(struct in_addr));
        return 1;
    }
    return 0;
}

static int read_until_prompt(int sock_fd, char *out_buf, size_t max_len, const char *target) {
    size_t len = 0;
    size_t target_len = strlen(target);
    while (len < max_len - 1) {
        char c;
        ssize_t n = recv(sock_fd, &c, 1, 0);
        if (n <= 0) return 0;
        out_buf[len++] = c;
        out_buf[len] = '\0';
        if (len >= target_len && strcmp(out_buf + len - target_len, target) == 0) {
            return 1;
        }
    }
    return 0;
}

static void print_usage(void) {
    puts("Usage: ssh [user@]hostname [-p port] [command]\n"
         "OpenSSH-compatible secure shell client for akaOS\n\n"
         "Options:\n"
         "  -p port      Port to connect to on remote host (default: 22)\n"
         "  -h, --help   Display this help message and exit\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int port = 22;
    const char *target = NULL;
    char command[512];
    command[0] = '\0';

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (!target) {
            target = argv[i];
        } else {
            /* Append remaining arguments as remote command */
            size_t cur = strlen(command);
            snprintf(command + cur, sizeof(command) - cur, "%s%s", cur > 0 ? " " : "", argv[i]);
        }
    }

    if (!target) {
        print_usage();
        return 1;
    }

    char username[AKAOS_ACCOUNT_NAME_MAX + 1];
    char hostname[128];
    const char *at = strchr(target, '@');
    if (at) {
        size_t ulen = (size_t)(at - target);
        if (ulen > AKAOS_ACCOUNT_NAME_MAX) ulen = AKAOS_ACCOUNT_NAME_MAX;
        memcpy(username, target, ulen);
        username[ulen] = '\0';
        strncpy(hostname, at + 1, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    } else {
        struct akaos_account self_acc;
        if (akaos_account_by_uid(geteuid(), &self_acc)) {
            strncpy(username, self_acc.name, sizeof(username) - 1);
            username[sizeof(username) - 1] = '\0';
        } else {
            strncpy(username, "root", sizeof(username) - 1);
            username[sizeof(username) - 1] = '\0';
        }
        strncpy(hostname, target, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    struct in_addr server_ip;
    if (!resolve_ip(hostname, &server_ip)) {
        fprintf(stderr, "ssh: Could not resolve hostname %s\n", hostname);
        return 1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("ssh: socket creation failed");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr = server_ip;
    saddr.sin_port = htons((uint16_t)port);

    if (connect(sock_fd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        fprintf(stderr, "ssh: connect to host %s port %d: Connection refused\n", hostname, port);
        close(sock_fd);
        return 1;
    }

    /* Banner exchange */
    char banner_buf[256];
    size_t b_idx = 0;
    while (b_idx < sizeof(banner_buf) - 1) {
        char c;
        if (recv(sock_fd, &c, 1, 0) <= 0) break;
        banner_buf[b_idx++] = c;
        if (c == '\n') break;
    }
    banner_buf[b_idx] = '\0';

    send(sock_fd, SSH_CLIENT_BANNER, strlen(SSH_CLIENT_BANNER), 0);

    /* Read "login: " */
    char prompt_buf[128];
    if (!read_until_prompt(sock_fd, prompt_buf, sizeof(prompt_buf), "login: ")) {
        fprintf(stderr, "ssh: handshake failed\n");
        close(sock_fd);
        return 1;
    }

    /* Send username */
    send(sock_fd, username, strlen(username), 0);
    send(sock_fd, "\n", 1, 0);

    /* Read "Password: " */
    if (!read_until_prompt(sock_fd, prompt_buf, sizeof(prompt_buf), "Password: ")) {
        fprintf(stderr, "ssh: auth protocol failed\n");
        close(sock_fd);
        return 1;
    }

    /* Prompt password without echo */
    char pass_prompt[128];
    snprintf(pass_prompt, sizeof(pass_prompt), "%s@%s's password: ", username, hostname);
    char password[AKAOS_PASSWORD_MAX + 1];
    if (!akaos_read_secret(pass_prompt, password, sizeof(password))) {
        close(sock_fd);
        return 1;
    }
    send(sock_fd, password, strlen(password), 0);
    send(sock_fd, "\n", 1, 0);
    memset(password, 0, sizeof(password));

    /* Check if remote command specified */
    if (command[0] != '\0') {
        /* Non-interactive command mode */
        send(sock_fd, command, strlen(command), 0);
        send(sock_fd, "\nexit\n", 6, 0);

        static char s_recv_buf[1024];
        while (1) {
            ssize_t n = recv(sock_fd, s_recv_buf, sizeof(s_recv_buf), 0);
            if (n <= 0) break;
            write(STDOUT_FILENO, s_recv_buf, (size_t)n);
        }
        close(sock_fd);
        return 0;
    }

    /* Interactive Session: put local terminal in raw mode */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (tcgetattr(STDIN_FILENO, &g_saved_termios) == 0) {
        g_termios_saved = 1;
        struct termios raw = g_saved_termios;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock_fd;
    fds[1].events = POLLIN;

    static uint8_t s_ssh_buf[1024];

    while (1) {
        int pr = poll(fds, 2, 500);
        if (pr < 0) break;

        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & (POLLERR | POLLHUP)) break;

        if (fds[0].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, s_ssh_buf, sizeof(s_ssh_buf));
            if (n <= 0) break;
            send(sock_fd, s_ssh_buf, (size_t)n, 0);
        }

        if (fds[1].revents & POLLIN) {
            ssize_t n = recv(sock_fd, s_ssh_buf, sizeof(s_ssh_buf), 0);
            if (n <= 0) break;
            write(STDOUT_FILENO, s_ssh_buf, (size_t)n);
        }
    }

    restore_terminal();
    close(sock_fd);
    printf("\r\nConnection to %s closed.\r\n", hostname);
    return 0;
}
