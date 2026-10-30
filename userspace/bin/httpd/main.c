#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

static void print_usage(void) {
    puts("Usage: httpd [options]\n"
         "Options:\n"
         "  -p <port>     Port to listen on (default: 80 or /etc/akaos.conf)\n"
         "  -d <dir>      Document root directory (default: /var/www)\n"
         "  -f            Run in foreground\n"
         "  --once        Handle single request and exit (for testing)\n"
         "  -h, --help    Display this help and exit\n"
         "  -v, --version Output version information and exit");
}

static int read_config(int *out_enabled, int *out_port) {
    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd < 0) return 0;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        if (strncmp(p, "HTTPD_ENABLED=", 14) == 0) {
            *out_enabled = (p[14] == '1');
        } else if (strncmp(p, "HTTPD_PORT=", 11) == 0) {
            *out_port = atoi(p + 11);
        }
        char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    int port = 80;
    int enabled = 0;
    int explicit_mode = 0;
    int run_once = 0;
    const char *docroot = "/var/www";

    read_config(&enabled, &port);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("akaOS microhttpd/1.0");
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            explicit_mode = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            docroot = argv[++i];
            explicit_mode = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            explicit_mode = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            run_once = 1;
            explicit_mode = 1;
        }
    }

    if (!explicit_mode && !enabled) {
        puts("httpd: service disabled in /etc/akaos.conf (run 'akaos-config' to enable)");
        return 0;
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        puts("httpd: failed to create server socket");
        return 1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = 0; /* INADDR_ANY */

    if (bind(server_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        printf("httpd: failed to bind to port %d\n", port);
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) < 0) {
        puts("httpd: listen failed");
        close(server_sock);
        return 1;
    }

    printf("akaOS microhttpd/1.0 listening on 0.0.0.0:%d (docroot: %s)\n", port, docroot);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client = accept(server_sock, (struct sockaddr *)&client_addr, &addrlen);
        if (client < 0) {
            poll(NULL, 0, 50);
            continue;
        }

        struct pollfd pfd;
        pfd.fd = client;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 2000);
        if (pr <= 0 || !(pfd.revents & POLLIN)) {
            close(client);
            if (run_once) break;
            continue;
        }

        char req_buf[1024];
        memset(req_buf, 0, sizeof(req_buf));
        ssize_t n = recv(client, req_buf, sizeof(req_buf) - 1, 0);
        if (n <= 0) {
            close(client);
            if (run_once) break;
            continue;
        }

        char method[16];
        char path[256];
        int midx = 0;
        const char *p = req_buf;
        while (*p && *p != ' ' && midx < (int)sizeof(method) - 1) {
            method[midx++] = *p++;
        }
        method[midx] = '\0';

        while (*p == ' ') p++;
        int pidx = 0;
        while (*p && *p != ' ' && *p != '?' && *p != '\r' && *p != '\n' && pidx < (int)sizeof(path) - 1) {
            path[pidx++] = *p++;
        }
        path[pidx] = '\0';

        if (strcmp(path, "/") == 0 || pidx == 0) {
            strncpy(path, "/index.html", sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }

        /* Check for path traversal */
        int has_dotdot = 0;
        for (int i = 0; path[i] && path[i+1]; ++i) {
            if (path[i] == '.' && path[i+1] == '.') { has_dotdot = 1; break; }
        }
        if (has_dotdot) {
            const char *forbidden = "HTTP/1.0 403 Forbidden\r\nServer: akaOS-httpd/1.0\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nForbidden\n";
            send(client, forbidden, strlen(forbidden), 0);
            close(client);
            if (run_once) break;
            continue;
        }

        char file_path[384];
        snprintf(file_path, sizeof(file_path), "%s%s", docroot, path);

        /* Dynamic CGI execution */
        if (strncmp(path, "/cgi-bin/", 9) == 0) {
            int pipe_fds[2];
            if (pipe(pipe_fds) == 0) {
                pid_t cgi_pid = fork();
                if (cgi_pid == 0) {
                    close(pipe_fds[0]);
                    dup2(pipe_fds[1], 1);
                    dup2(pipe_fds[1], 2);
                    close(pipe_fds[1]);
                    close(client);
                    close(server_sock);
                    char *cgi_argv[] = {file_path, NULL};
                    execv(file_path, cgi_argv);
                    exit(1);
                }
                close(pipe_fds[1]);
                char cgi_buf[2048];
                ssize_t cgi_len = read(pipe_fds[0], cgi_buf, sizeof(cgi_buf) - 1);
                close(pipe_fds[0]);
                waitpid(cgi_pid, NULL, 0);

                if (cgi_len > 0) {
                    cgi_buf[cgi_len] = '\0';
                    if (strncmp(cgi_buf, "HTTP/", 5) != 0) {
                        char header[256];
                        snprintf(header, sizeof(header),
                                 "HTTP/1.0 200 OK\r\n"
                                 "Server: akaOS-microhttpd/1.0\r\n"
                                 "Content-Type: text/html\r\n"
                                 "Content-Length: %d\r\n"
                                 "Connection: close\r\n\r\n",
                                 (int)cgi_len);
                        send(client, header, strlen(header), 0);
                    }
                    send(client, cgi_buf, (size_t)cgi_len, 0);
                } else {
                    const char *cgi_err = "HTTP/1.0 500 Internal Server Error\r\nServer: akaOS-microhttpd/1.0\r\nConnection: close\r\n\r\nCGI execution failed\n";
                    send(client, cgi_err, strlen(cgi_err), 0);
                }
            }
            close(client);
            if (run_once) break;
            continue;
        }

        int fd = open(file_path, O_RDONLY);
        if (fd >= 0) {
            char file_data[4096];
            ssize_t file_len = read(fd, file_data, sizeof(file_data));
            close(fd);
            if (file_len < 0) file_len = 0;

            char header[256];
            snprintf(header, sizeof(header),
                     "HTTP/1.0 200 OK\r\n"
                     "Server: akaOS-microhttpd/1.0\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n\r\n",
                     (int)file_len);

            send(client, header, strlen(header), 0);
            if (strcmp(method, "HEAD") != 0 && file_len > 0) {
                send(client, file_data, (size_t)file_len, 0);
            }
        } else {
            const char *not_found =
                "HTTP/1.0 404 Not Found\r\n"
                "Server: akaOS-microhttpd/1.0\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n\r\n"
                "<!DOCTYPE html><html><body><h1>404 Not Found</h1><p>The requested file was not found on akaOS.</p></body></html>\n";
            send(client, not_found, strlen(not_found), 0);
        }

        close(client);
        if (run_once) break;
    }

    close(server_sock);
    return 0;
}
