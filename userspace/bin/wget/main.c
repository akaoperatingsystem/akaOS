/*
 * wget - akaOS Native HTTP File Retrieval Utility
 *
 * Copyright (c) 2026 akaOS Authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <akaos/version.h>
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
    puts("GNU Wget 1.21.4 (akaOS native)\n"
         "Usage: wget [OPTION]... [URL]...\n\n"
         "Startup:\n"
         "  -V,  --version           display the version of Wget and exit\n"
         "  -h,  --help              print this help\n\n"
         "Logging and input file:\n"
         "  -q,  --quiet             quiet (no output)\n"
         "  -v,  --verbose           be verbose (this is the default)\n"
         "  -nv, --no-verbose        turn off verboseness, without being quiet\n\n"
         "Download:\n"
         "  -O,  --output-document=FILE    write documents to FILE\n"
         "  -c,  --continue                resume getting a partially-downloaded file\n"
         "       --spider                  don't download anything\n\n"
         "Directories:\n"
         "  -P,  --directory-prefix=PREFIX save files to PREFIX/...\n");
}

/* Extract filename from URL path, default to 'index.html' */
static const char *get_default_filename(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash && *(slash + 1) != '\0') {
        return slash + 1;
    }
    return "index.html";
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "wget: missing URL\nUsage: wget [OPTION]... [URL]...\n\nTry 'wget --help' for more options.\n");
        return 1;
    }

    int quiet = 0;
    int verbose = 1; (void)verbose;
    int spider = 0;
    int continue_dl = 0;
    const char *output_file = NULL;
    const char *directory_prefix = NULL;
    const char *url = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("GNU Wget 1.21.4 built on akaOS %s (XSCore %s)\n", AKAOS_VERSION, XSCORE_VERSION);
            puts("+ipv4 +http +poll -https -ssl -ipv6 -iri");
            return 0;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
            verbose = 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            quiet = 0;
        } else if (strcmp(argv[i], "-nv") == 0 || strcmp(argv[i], "--no-verbose") == 0) {
            verbose = 0;
            quiet = 0;
        } else if (strcmp(argv[i], "--spider") == 0) {
            spider = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--continue") == 0) {
            continue_dl = 1;
        } else if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strncmp(argv[i], "--output-document=", 18) == 0) {
            output_file = argv[i] + 18;
        } else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            directory_prefix = argv[++i];
        } else if (strncmp(argv[i], "--directory-prefix=", 19) == 0) {
            directory_prefix = argv[i] + 19;
        } else if (argv[i][0] != '-') {
            url = argv[i];
        } else {
            fprintf(stderr, "wget: unrecognized option '%s'\nTry 'wget --help' for more options.\n", argv[i]);
            return 1;
        }
    }

    if (!url) {
        fprintf(stderr, "wget: missing URL\nUsage: wget [OPTION]... [URL]...\n\nTry 'wget --help' for more options.\n");
        return 1;
    }

    if (strncmp(url, "https://", 8) == 0) {
        if (!quiet) {
            fprintf(stderr, "HTTPS support not compiled in\n");
            fprintf(stderr, "Cannot write to '%s' (Unsupported scheme).\n", url);
        }
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
        if (!quiet) fprintf(stderr, "%s: Unsupported scheme.\n", url);
        return 1;
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

    if (!quiet) {
        printf("--2026-11-01--  %s\n", url);
        printf("Resolving %s... ", host);
        fflush(stdout);
    }

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr) {
        if (!quiet) printf("failed: Name or service not known.\nwget: unable to resolve host address '%s'\n", host);
        return 4;
    }

    uint32_t target_ip;
    memcpy(&target_ip, he->h_addr, sizeof(uint32_t));

    struct in_addr in_a;
    in_a.s_addr = target_ip;
    if (!quiet) {
        printf("%s\n", inet_ntoa(in_a));
        printf("Connecting to %s|%s|:%d... ", host, inet_ntoa(in_a), port);
        fflush(stdout);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (!quiet) puts("failed: Cannot create socket.");
        return 3;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = target_ip;

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        if (!quiet) printf("failed: Connection refused.\n");
        close(sock);
        return 3;
    }

    if (!quiet) {
        puts("connected.");
        printf("HTTP request sent, awaiting response... ");
        fflush(stdout);
    }

    char request[512];
    snprintf(request, sizeof(request),
             "%s %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: Wget/1.21.4 (akaOS %s)\r\n"
             "Accept: */*\r\n"
             "Accept-Encoding: identity\r\n"
             "Connection: close\r\n\r\n",
             spider ? "HEAD" : "GET",
             path, host, AKAOS_VERSION);

    if (send(sock, request, strlen(request), 0) < 0) {
        if (!quiet) puts("failed: Error sending request.");
        close(sock);
        return 3;
    }

    /* Read HTTP response status */
    char line_buf[256];
    int line_len = 0;
    char ch;
    int status_code = 0;

    while (read(sock, &ch, 1) == 1) {
        if (ch == '\r') continue;
        if (ch == '\n') break;
        if (line_len < (int)sizeof(line_buf) - 1) {
            line_buf[line_len++] = ch;
        }
    }
    line_buf[line_len] = '\0';

    if (strncmp(line_buf, "HTTP/", 5) == 0) {
        char *sp = strchr(line_buf, ' ');
        if (sp) status_code = atoi(sp + 1);
    }

    if (!quiet) {
        printf("%s\n", line_buf[0] ? line_buf : "200 OK");
    }

    if (status_code >= 400) {
        if (!quiet) fprintf(stderr, "%s ERROR %d: %s.\n", line_buf, status_code, line_buf);
        close(sock);
        return 8;
    }

    /* Parse remaining headers until empty line */
    long content_length = -1;
    char header_line[256];
    int hlen = 0;

    for (;;) {
        hlen = 0;
        while (read(sock, &ch, 1) == 1) {
            if (ch == '\r') continue;
            if (ch == '\n') break;
            if (hlen < (int)sizeof(header_line) - 1) {
                header_line[hlen++] = ch;
            }
        }
        header_line[hlen] = '\0';
        if (hlen == 0) break;

        if (strncasecmp(header_line, "Content-Length:", 15) == 0) {
            content_length = atol(header_line + 15);
        }
    }

    if (!quiet) {
        if (content_length >= 0) {
            printf("Length: %ld (%ldK)\n", content_length, content_length / 1024);
        } else {
            puts("Length: unspecified");
        }
    }

    if (spider) {
        if (!quiet) puts("Remote file exists.");
        close(sock);
        return 0;
    }

    /* Determine destination file */
    char final_dest[256];
    if (output_file) {
        if (strcmp(output_file, "-") == 0) {
            strcpy(final_dest, "-");
        } else {
            strncpy(final_dest, output_file, sizeof(final_dest) - 1);
            final_dest[sizeof(final_dest) - 1] = '\0';
        }
    } else {
        const char *def_name = get_default_filename(path);
        if (directory_prefix) {
            snprintf(final_dest, sizeof(final_dest), "%s/%s", directory_prefix, def_name);
        } else {
            strncpy(final_dest, def_name, sizeof(final_dest) - 1);
            final_dest[sizeof(final_dest) - 1] = '\0';
        }
    }

    int out_fd = STDOUT_FILENO;
    if (strcmp(final_dest, "-") != 0) {
        int flags = O_WRONLY | O_CREAT;
        if (continue_dl) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        out_fd = open(final_dest, flags, 0644);
        if (out_fd < 0) {
            if (!quiet) fprintf(stderr, "%s: Cannot open for writing\n", final_dest);
            close(sock);
            return 3;
        }
        if (!quiet) printf("Saving to: '%s'\n\n", final_dest);
    }

    char buffer[2048];
    long total_saved = 0;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 5000);
        if (pr <= 0 || !(pfd.revents & POLLIN)) {
            break;
        }

        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;

        write(out_fd, buffer, (size_t)n);
        total_saved += n;
    }

    if (out_fd != STDOUT_FILENO) {
        close(out_fd);
    }
    close(sock);

    if (!quiet && strcmp(final_dest, "-") != 0) {
        printf("\n2026-11-01 -- '%s' saved [%ld]\n\n", final_dest, total_saved);
    }

    return 0;
}
