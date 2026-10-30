#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>


#define TELNET_IAC  255
#define TELNET_DONT 254
#define TELNET_DO   253
#define TELNET_WONT 252
#define TELNET_WILL 251

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

static void filter_telnet_input(int client_fd, int master_fd, uint8_t *buf, int len) {
    uint8_t out[1024];
    int out_len = 0;

    for (int i = 0; i < len; ++i) {
        if (buf[i] == TELNET_IAC) {
            if (i + 2 < len) {
                uint8_t cmd = buf[i + 1];
                uint8_t opt = buf[i + 2];
                uint8_t resp[3];
                resp[0] = TELNET_IAC;
                if (cmd == TELNET_DO) {
                    resp[1] = TELNET_WONT;
                    resp[2] = opt;
                    send(client_fd, resp, 3, 0);
                } else if (cmd == TELNET_WILL) {
                    resp[1] = TELNET_DONT;
                    resp[2] = opt;
                    send(client_fd, resp, 3, 0);
                }
                i += 2;
                continue;
            }
        }
        /* CR/LF conversion if needed */
        if (buf[i] == '\r') {
            out[out_len++] = '\n';
            if (i + 1 < len && buf[i + 1] == '\0') i++; /* Skip telnet CR-NUL */
        } else {
            out[out_len++] = buf[i];
        }
        if (out_len >= (int)sizeof(out)) {
            write(master_fd, out, out_len);
            out_len = 0;
        }
    }
    if (out_len > 0) {
        write(master_fd, out, out_len);
    }
}

static void handle_session(int client_fd) {
    int master_fd = open("/dev/ptmx", O_RDWR);
    if (master_fd < 0) {
        const char *err = "telnetd: failed to allocate PTY\r\n";
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
            tcsetpgrp(0, getpid());
            char *args[] = {"/bin/login", NULL};
            execv("/bin/login", args);
            exit(1);
        }
        exit(1);
    }

    /* Send Telnet negotiation to disable echo (PTY line discipline handles it) */
    uint8_t init_neg[] = {
        TELNET_IAC, TELNET_WILL, 1, /* WILL ECHO */
        TELNET_IAC, TELNET_WILL, 3  /* WILL SUPPRESS GO AHEAD */
    };
    send(client_fd, init_neg, sizeof(init_neg), 0);

    struct pollfd fds[2];
    fds[0].fd = client_fd;
    fds[0].events = POLLIN;
    fds[1].fd = master_fd;
    fds[1].events = POLLIN;

    uint8_t buffer[1024];

    while (1) {
        int pr = poll(fds, 2, 500);
        if (pr < 0) break;

        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & (POLLERR | POLLHUP)) break;

        if (fds[0].revents & POLLIN) {
            ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            filter_telnet_input(client_fd, master_fd, buffer, (int)n);
        }

        if (fds[1].revents & POLLIN) {
            ssize_t n = read(master_fd, buffer, sizeof(buffer));
            if (n <= 0) break;
            /* Send to client */
            send(client_fd, buffer, (size_t)n, 0);
        }

        /* Check if child exited */
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
    int port = parse_config_int("TELNETD_PORT", 23);
    int enabled = parse_config_int("TELNETD_ENABLED", 0);
    int force_run = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            force_run = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            force_run = 1;
        } else if (strcmp(argv[i], "--status") == 0) {
            printf("TELNETD is %s (port %d)\n", enabled ? "ENABLED" : "DISABLED", port);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: telnetd [-p port] [-f] [--status]\n");
            return 0;
        }
    }

    if (!enabled && !force_run) {
        printf("telnetd: disabled in /etc/akaos.conf (use akaos-config --enable-telnetd)\n");
        return 0;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("telnetd: socket creation failed");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("telnetd: bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 4) != 0) {
        perror("telnetd: listen failed");
        close(server_fd);
        return 1;
    }

    printf("[telnetd] Remote shell daemon listening on port %d\n", port);

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
