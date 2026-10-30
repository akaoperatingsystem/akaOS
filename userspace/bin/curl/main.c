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
    puts("Usage: curl [options...] <url>\n"
         "Options:\n"
         "  -i, --include      Include protocol response headers in the output\n"
         "  -I, --head         Show document info only\n"
         "  -s, --silent       Silent mode (don't output progress/diagnostics)\n"
         "  -o, --output FILE  Write to FILE instead of stdout\n"
         "  -h, --help         Display this help and exit\n"
         "  -v, --version      Output version information and exit");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int include_headers = 0;
    int head_only = 0;
    int silent = 0;
    const char *output_file = NULL;
    const char *url = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("curl 7.88.1 (akaOS) libcurl/7.88.1-micro");
            return 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include") == 0) {
            include_headers = 1;
        } else if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--head") == 0) {
            head_only = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0) {
            silent = 1;
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            url = argv[i];
        }
    }

    if (!url) {
        if (!silent) puts("curl: no URL specified!");
        return 2;
    }

    if (strncmp(url, "https://", 8) == 0) {
        if (!silent) printf("curl: (1) Protocol \"https\" not supported or disabled in akaOS microcurl\n");
        return 1;
    }

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    char host[128];
    int port = 80;
    char path[256];
    path[0] = '/';
    path[1] = '\0';

    int hidx = 0;
    while (*p && *p != ':' && *p != '/' && hidx < (int)sizeof(host) - 1) {
        host[hidx++] = *p++;
    }
    host[hidx] = '\0';

    if (hidx == 0) {
        if (!silent) puts("curl: (3) URL using bad/illegal format or missing host");
        return 3;
    }

    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port <= 0 || port > 65535) port = 80;
    }

    if (*p == '/') {
        strncpy(path, p, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr) {
        if (!silent) printf("curl: (6) Could not resolve host: %s\n", host);
        return 6;
    }

    uint32_t target_ip;
    memcpy(&target_ip, he->h_addr, sizeof(uint32_t));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (!silent) puts("curl: (7) Failed to create socket");
        return 7;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = target_ip;

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        if (!silent) printf("curl: (7) Failed to connect to %s port %d\n", host, port);
        close(sock);
        return 7;
    }

    char request[512];
    snprintf(request, sizeof(request),
             "%s %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: curl/7.88.1 (akaOS)\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             head_only ? "HEAD" : "GET",
             path, host);

    if (send(sock, request, strlen(request), 0) < 0) {
        if (!silent) puts("curl: (55) Failed sending HTTP request");
        close(sock);
        return 55;
    }

    int out_fd = STDOUT_FILENO;
    if (output_file) {
        out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            if (!silent) printf("curl: (23) Failed to open output file %s\n", output_file);
            close(sock);
            return 23;
        }
    }

    char buffer[1024];
    int in_body = (include_headers || head_only) ? 1 : 0;
    int header_buf_pos = 0;
    char header_buf[2048];

    for (;;) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 3000);
        if (pr <= 0 || !(pfd.revents & POLLIN)) {
            break; /* Timeout or done */
        }

        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;

        if (in_body) {
            write(out_fd, buffer, (size_t)n);
        } else {
            /* Accumulate to strip HTTP header */
            for (ssize_t i = 0; i < n; ++i) {
                if (in_body) {
                    write(out_fd, &buffer[i], 1);
                } else {
                    if (header_buf_pos < (int)sizeof(header_buf) - 1) {
                        header_buf[header_buf_pos++] = buffer[i];
                        header_buf[header_buf_pos] = '\0';
                        if (header_buf_pos >= 4 &&
                            header_buf[header_buf_pos - 4] == '\r' &&
                            header_buf[header_buf_pos - 3] == '\n' &&
                            header_buf[header_buf_pos - 2] == '\r' &&
                            header_buf[header_buf_pos - 1] == '\n') {
                            in_body = 1;
                        }
                    } else {
                        in_body = 1;
                        write(out_fd, &buffer[i], 1);
                    }
                }
            }
        }
    }

    if (out_fd != STDOUT_FILENO) {
        close(out_fd);
    }
    close(sock);
    return 0;
}
