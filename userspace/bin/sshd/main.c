#include <akaos/account.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define SSH_BANNER "SSH-2.0-akaOS_SSHD_1.0\r\n"

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

static int read_client_line(int client_fd, char *buf, size_t max_len) {
    size_t len = 0;
    while (len < max_len - 1) {
        char c;
        ssize_t n = recv(client_fd, &c, 1, 0);
        if (n <= 0) return 0;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[len++] = c;
    }
    buf[len] = '\0';
    return 1;
}

static void handle_session(int client_fd) {
    /* Step 1: Banner exchange */
    send(client_fd, SSH_BANNER, strlen(SSH_BANNER), 0);

    char client_banner[256];
    if (!read_client_line(client_fd, client_banner, sizeof(client_banner))) {
        close(client_fd);
        return;
    }

    /* Step 2: Authentication (up to 3 attempts) */
    struct akaos_account acc;
    int authenticated = 0;

    for (int attempt = 0; attempt < 3; ++attempt) {
        const char *login_prompt = "login: ";
        send(client_fd, login_prompt, strlen(login_prompt), 0);

        char username[AKAOS_ACCOUNT_NAME_MAX + 1];
        if (!read_client_line(client_fd, username, sizeof(username))) break;

        const char *pass_prompt = "Password: ";
        send(client_fd, pass_prompt, strlen(pass_prompt), 0);

        char password[AKAOS_PASSWORD_MAX + 1];
        if (!read_client_line(client_fd, password, sizeof(password))) break;

        if (akaos_account_by_name(username, &acc) && akaos_account_verify(&acc, password)) {
            authenticated = 1;
            memset(password, 0, sizeof(password));
            break;
        }
        memset(password, 0, sizeof(password));
        const char *fail_msg = "Permission denied, please try again.\r\n";
        send(client_fd, fail_msg, strlen(fail_msg), 0);
    }

    if (!authenticated) {
        const char *denied = "Permission denied (publickey,password).\r\n";
        send(client_fd, denied, strlen(denied), 0);
        close(client_fd);
        return;
    }

    const char *welcome = "Welcome to akaOS OpenSSH Server\r\n\r\n";
    send(client_fd, welcome, strlen(welcome), 0);

    /* Step 3: Allocate PTY */
    int master_fd = open("/dev/ptmx", O_RDWR);
    if (master_fd < 0) {
        const char *err = "sshd: failed to allocate PTY\r\n";
        send(client_fd, err, strlen(err), 0);
        close(client_fd);
        return;
    }

    char *pts = ptsname(master_fd);
    if (!pts) {
        close(master_fd);
        close(client_fd);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int slave_fd = open(pts, O_RDWR);
        if (slave_fd >= 0) {
            dup2(slave_fd, 0);
            dup2(slave_fd, 1);
            dup2(slave_fd, 2);
            if (slave_fd > 2) close(slave_fd);
            close(master_fd);
            close(client_fd);

            if (setgid(acc.gid) != 0 || setuid(acc.uid) != 0) {
                _exit(1);
            }
            if (chdir(acc.home) != 0) {
                (void)chdir("/");
            }
            tcsetpgrp(0, getpid());

            char *shell_path = acc.shell[0] ? acc.shell : "/bin/akash";
            char *args[] = {shell_path, NULL};
            execv(shell_path, args);
            _exit(1);
        }
        _exit(1);
    }

    /* Step 4: Bidirectional I/O pumping between socket and PTY master */
    struct pollfd fds[2];
    fds[0].fd = client_fd;
    fds[0].events = POLLIN;
    fds[1].fd = master_fd;
    fds[1].events = POLLIN;

    static uint8_t s_io_buf[1024];

    while (1) {
        int pr = poll(fds, 2, 500);
        if (pr < 0) break;

        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & (POLLERR | POLLHUP)) break;

        if (fds[0].revents & POLLIN) {
            ssize_t n = recv(client_fd, s_io_buf, sizeof(s_io_buf), 0);
            if (n <= 0) break;
            write(master_fd, s_io_buf, (size_t)n);
        }

        if (fds[1].revents & POLLIN) {
            ssize_t n = read(master_fd, s_io_buf, sizeof(s_io_buf));
            if (n <= 0) break;
            send(client_fd, s_io_buf, (size_t)n, 0);
        }

        int status;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            break;
        }
    }

    close(master_fd);
    close(client_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

int main(int argc, char **argv) {
    int port = parse_config_int("SSHD_PORT", 22);
    int enabled = parse_config_int("SSHD_ENABLED", 0);
    int force_run = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            force_run = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            force_run = 1;
        } else if (strcmp(argv[i], "--status") == 0) {
            printf("SSHD is %s (port %d)\n", enabled ? "ENABLED" : "DISABLED", port);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: sshd [-p port] [-f] [--status]\n");
            return 0;
        }
    }

    if (!enabled && !force_run) {
        printf("sshd: disabled in /etc/akaos.conf (use akaos-config --enable-sshd)\n");
        return 0;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("sshd: socket creation failed");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("sshd: bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 4) != 0) {
        perror("sshd: listen failed");
        close(server_fd);
        return 1;
    }

    printf("[sshd] OpenSSH-compatible SSH daemon listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            handle_session(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
