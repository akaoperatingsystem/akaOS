#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CURL_VERSION_STR "curl 7.88.1 (akaOS) libcurl/7.88.1-enhanced"

/* Command options */
static const char *opt_user_agent = "curl/7.88.1 (akaOS)";
static const char *opt_cookie = NULL;
static const char *opt_cookie_jar = NULL;
static int opt_compressed = 0;
static const char *opt_data = NULL;
static int opt_fail = 0;
static const char *opt_form = NULL;
static const char *opt_headers[32];
static int opt_header_count = 0;
static int opt_head_only = 0;
static int opt_include_headers = 0;
static int opt_insecure = 0;
static const char *opt_limit_rate = NULL;
static int opt_location = 0;
static int opt_max_time = 0;
static const char *opt_output = NULL;
static int opt_remote_name = 0;
static int opt_silent = 0;
static int opt_show_error = 0;
static const char *opt_upload_file = NULL;
static const char *opt_user = NULL;
static int opt_verbose = 0;
static const char *opt_write_out = NULL;
static const char *opt_proxy = NULL;

static void print_version(void) {
    puts(CURL_VERSION_STR);
}

static void print_usage(void) {
    puts("Usage: curl [options...] <url>\n"
         "Options:\n"
         "  -A, --user-agent <string> Send User-Agent <string> to server\n"
         "  -b, --cookie <data>       Send cookies from string/file\n"
         "  -c, --cookie-jar <file>   Write cookies to <file> after a transfer\n"
         "      --compressed          Request compressed response\n"
         "  -d, --data <data>         HTTP POST data\n"
         "  -f, --fail                Fail silently (no output at all) on HTTP errors\n"
         "  -F, --form <name=content> Emulate a filled-in form\n"
         "  -H, --header <header>     Pass custom header(s) to server\n"
         "  -i, --include             Include protocol response headers in the output\n"
         "  -I, --head                Show document info only\n"
         "  -k, --insecure            Allow insecure server connections\n"
         "      --limit-rate <speed>  Limit transfer speed to rate\n"
         "  -L, --location            Follow redirects\n"
         "  -m, --max-time <seconds>  Maximum time allowed for the transfer\n"
         "  -o, --output <file>       Write to file instead of stdout\n"
         "  -O, --remote-name         Write output to file named as remote file\n"
         "  -s, --silent              Silent mode\n"
         "  -S, --show-error          Show error even when -s is used\n"
         "  -T, --upload-file <file>  Transfer local FILE to destination\n"
         "  -u, --user <user:pwd>     Server user and password\n"
         "  -v, --verbose             Make the operation more talkative\n"
         "  -w, --write-out <format>  Use output FORMAT after completion\n"
         "  -x, --proxy <[prot://]host[:port]> Use proxy on given port\n"
         "  -h, --help                This help text\n"
         "  -V, --version             Show version number and exit");
}

/* Base64 encode for Basic Auth */
static void base64_encode(const char *src, char *dst, size_t dst_len) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t srclen = strlen(src);
    size_t di = 0;
    for (size_t si = 0; si < srclen; si += 3) {
        if (di + 4 >= dst_len) break;
        uint32_t val = (uint8_t)src[si] << 16;
        if (si + 1 < srclen) val |= (uint8_t)src[si + 1] << 8;
        if (si + 2 < srclen) val |= (uint8_t)src[si + 2];
        dst[di++] = tbl[(val >> 18) & 0x3F];
        dst[di++] = tbl[(val >> 12) & 0x3F];
        dst[di++] = (si + 1 < srclen) ? tbl[(val >> 6) & 0x3F] : '=';
        dst[di++] = (si + 2 < srclen) ? tbl[val & 0x3F] : '=';
    }
    dst[di] = '\0';
}

static int parse_url(const char *url, char *host, int *port, char *path) {
    if (!url) return -1;
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
    }

    int hidx = 0;
    while (*p && *p != ':' && *p != '/' && hidx < 127) {
        host[hidx++] = *p++;
    }
    host[hidx] = '\0';
    if (hidx == 0) return -1;

    if (*p == ':') {
        p++;
        int pt = 0;
        while (*p >= '0' && *p <= '9') {
            pt = pt * 10 + (*p - '0');
            p++;
        }
        if (pt > 0 && pt <= 65535) *port = pt;
    }

    if (*p == '/') {
        strncpy(path, p, 255);
        path[255] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    return 0;
}

/* Compare header names without case or whitespace before the colon. */
static int credential_header(const char *header) {
    const char *colon = strchr(header, ':');
    if (!colon) return 0;
    while (*header == ' ' || *header == '\t') header++;
    const char *end = colon;
    while (end > header && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t len = (size_t)(end - header);
    return (len == 13 && strncasecmp(header, "Authorization", len) == 0) ||
           (len == 6 && strncasecmp(header, "Cookie", len) == 0);
}

static int fetch_url(const char *url, const char *credential_url) {
    char host[128];
    int port = 80;
    char path[256];

    if (parse_url(url, host, &port, path) < 0) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (3) URL using bad/illegal format or missing host\n");
        }
        return 3;
    }

    /* Credentials belong to the initial origin, even across multiple redirects. */
    char credential_host[128], credential_path[256];
    int credential_port = 80;
    int send_credentials =
        parse_url(credential_url, credential_host, &credential_port, credential_path) == 0 &&
        strcasecmp(host, credential_host) == 0 && port == credential_port &&
        (strncmp(url, "https://", 8) == 0) ==
        (strncmp(credential_url, "https://", 8) == 0);

    if (strncmp(url, "https://", 8) == 0 && !opt_insecure) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (1) Protocol \"https\" not supported or disabled in microcurl\n");
        }
        return 1;
    }

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (6) Could not resolve host: %s\n", host);
        }
        return 6;
    }

    uint32_t target_ip;
    memcpy(&target_ip, he->h_addr, sizeof(uint32_t));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (7) Failed to create socket\n");
        }
        return 7;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = target_ip;

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (7) Failed to connect to %s port %d: %s\n", host, port, strerror(errno));
        }
        close(sock);
        return 7;
    }

    if (opt_verbose) {
        fprintf(stderr, "* Connected to %s (%s) port %d\n", host, inet_ntoa(sin.sin_addr), port);
    }

    /* Build HTTP Request */
    char req[4096];
    const char *method = "GET";
    if (opt_head_only) method = "HEAD";
    else if (opt_data || opt_form) method = "POST";
    else if (opt_upload_file) method = "PUT";

    int req_len = snprintf(req, sizeof(req),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n",
        method, path, host, opt_user_agent);

    if (opt_cookie && send_credentials) {
        req_len += snprintf(req + req_len, sizeof(req) - req_len, "Cookie: %s\r\n", opt_cookie);
    }

    if (opt_compressed) {
        req_len += snprintf(req + req_len, sizeof(req) - req_len, "Accept-Encoding: gzip, deflate\r\n");
    }

    if (opt_user && send_credentials) {
        char b64[256];
        base64_encode(opt_user, b64, sizeof(b64));
        req_len += snprintf(req + req_len, sizeof(req) - req_len, "Authorization: Basic %s\r\n", b64);
    }

    for (int i = 0; i < opt_header_count; ++i) {
        if (!send_credentials && credential_header(opt_headers[i])) continue;
        req_len += snprintf(req + req_len, sizeof(req) - req_len, "%s\r\n", opt_headers[i]);
    }

    if (opt_data) {
        req_len += snprintf(req + req_len, sizeof(req) - req_len,
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %zu\r\n\r\n%s",
            strlen(opt_data), opt_data);
    } else {
        req_len += snprintf(req + req_len, sizeof(req) - req_len, "\r\n");
    }

    if (opt_verbose) {
        fprintf(stderr, "> %s %s HTTP/1.1\n> Host: %s\n> User-Agent: %s\n> \n", method, path, host, opt_user_agent);
    }

    if (send(sock, req, (size_t)req_len, 0) < 0) {
        if (!opt_silent || opt_show_error) {
            fprintf(stderr, "curl: (55) Failed sending HTTP request\n");
        }
        close(sock);
        return 55;
    }

    /* Open Output */
    const char *out_name = opt_output;
    
    if (opt_remote_name && !out_name) {
        const char *slash = strrchr(path, '/');
        if (slash && *(slash + 1)) {
            out_name = slash + 1;
        } else {
            out_name = "index.html";
        }
    }

    int out_fd = STDOUT_FILENO;
    if (out_name && strcmp(out_name, "-") != 0) {
        out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            if (!opt_silent || opt_show_error) {
                fprintf(stderr, "curl: (23) Failed to open output file %s: %s\n", out_name, strerror(errno));
            }
            close(sock);
            return 23;
        }
    }

    char buffer[2048];
    int in_body = (opt_include_headers || opt_head_only) ? 1 : 0;
    char header_accum[4096];
    int header_accum_len = 0;
    int http_status = 0;
    char redirect_url[256];
    redirect_url[0] = '\0';
    size_t total_body_bytes = 0;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int timeout_ms = (opt_max_time > 0) ? (opt_max_time * 1000) : 5000;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0 || !(pfd.revents & POLLIN)) {
            break;
        }

        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;

        if (in_body) {
            write(out_fd, buffer, (size_t)n);
            total_body_bytes += (size_t)n;
        } else {
            for (ssize_t i = 0; i < n; ++i) {
                if (in_body) {
                    write(out_fd, &buffer[i], 1);
                    total_body_bytes++;
                } else {
                    if (header_accum_len + 1 < (int)sizeof(header_accum)) {
                        header_accum[header_accum_len++] = buffer[i];
                        header_accum[header_accum_len] = '\0';

                        if (header_accum_len >= 4 &&
                            header_accum[header_accum_len - 4] == '\r' &&
                            header_accum[header_accum_len - 3] == '\n' &&
                            header_accum[header_accum_len - 2] == '\r' &&
                            header_accum[header_accum_len - 1] == '\n') {
                            in_body = 1;

                            /* Parse status */
                            if (sscanf(header_accum, "HTTP/%*s %d", &http_status) == 1) {
                                if (opt_verbose) {
                                    fprintf(stderr, "< HTTP status %d\n", http_status);
                                }
                            }

                            /* Parse Location for redirect */
                            char *loc = strstr(header_accum, "Location:");
                            if (!loc) loc = strstr(header_accum, "location:");
                            if (loc) {
                                loc += 9;
                                while (*loc == ' ' || *loc == '\t') loc++;
                                char *endloc = strstr(loc, "\r\n");
                                if (endloc) {
                                    size_t llen = (size_t)(endloc - loc);
                                    if (llen < sizeof(redirect_url)) {
                                        strncpy(redirect_url, loc, llen);
                                        redirect_url[llen] = '\0';
                                    }
                                }
                            }

                            /* Parse Set-Cookie for cookie-jar */
                            if (opt_cookie_jar) {
                                char *sc = strstr(header_accum, "Set-Cookie:");
                                if (!sc) sc = strstr(header_accum, "set-cookie:");
                                if (sc) {
                                    sc += 11;
                                    while (*sc == ' ' || *sc == '\t') sc++;
                                    char *endsc = strstr(sc, "\r\n");
                                    if (endsc) {
                                        int cjfd = open(opt_cookie_jar, O_WRONLY | O_CREAT | O_APPEND, 0644);
                                        if (cjfd >= 0) {
                                            write(cjfd, sc, (size_t)(endsc - sc));
                                            write(cjfd, "\n", 1);
                                            close(cjfd);
                                        }
                                    }
                                }
                            }

                            if (opt_fail && http_status >= 400) {
                                close(sock);
                                if (out_fd != STDOUT_FILENO) close(out_fd);
                                return 22;
                            }
                        }
                    } else {
                        in_body = 1;
                        write(out_fd, &buffer[i], 1);
                        total_body_bytes++;
                    }
                }
            }
        }
    }

    if (out_fd != STDOUT_FILENO) {
        close(out_fd);
    }
    close(sock);

    if (opt_location && (http_status == 301 || http_status == 302 || http_status == 303 || http_status == 307 || http_status == 308)) {
        if (redirect_url[0] != '\0') {
            if (opt_verbose) fprintf(stderr, "* Issue redirect to: %s\n", redirect_url);
            return fetch_url(redirect_url, credential_url);
        }
    }

    if (opt_write_out) {
        for (const char *w = opt_write_out; *w; ++w) {
            if (*w == '%' && *(w + 1) == '{') {
                const char *endvar = strchr(w + 2, '}');
                if (endvar) {
                    char var[64];
                    size_t vlen = (size_t)(endvar - (w + 2));
                    if (vlen < sizeof(var)) {
                        strncpy(var, w + 2, vlen);
                        var[vlen] = '\0';
                        if (strcmp(var, "http_code") == 0 || strcmp(var, "response_code") == 0) {
                            printf("%03d", http_status);
                        } else if (strcmp(var, "size_download") == 0) {
                            printf("%zu", total_body_bytes);
                        } else if (strcmp(var, "url_effective") == 0) {
                            printf("%s", url);
                        }
                    }
                    w = endvar;
                    continue;
                }
            } else if (*w == '\\' && *(w + 1) == 'n') {
                putchar('\n');
                w++;
            } else if (*w == '\\' && *(w + 1) == 't') {
                putchar('\t');
                w++;
            } else {
                putchar(*w);
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *url = NULL;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--include") == 0) {
            opt_include_headers = 1;
        } else if (strcmp(arg, "-I") == 0 || strcmp(arg, "--head") == 0) {
            opt_head_only = 1;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--silent") == 0) {
            opt_silent = 1;
        } else if (strcmp(arg, "-S") == 0 || strcmp(arg, "--show-error") == 0) {
            opt_show_error = 1;
        } else if (strcmp(arg, "-k") == 0 || strcmp(arg, "--insecure") == 0) {
            opt_insecure = 1;
        } else if (strcmp(arg, "-L") == 0 || strcmp(arg, "--location") == 0) {
            opt_location = 1;
        } else if (strcmp(arg, "-O") == 0 || strcmp(arg, "--remote-name") == 0) {
            opt_remote_name = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--fail") == 0) {
            opt_fail = 1;
        } else if (strcmp(arg, "--compressed") == 0) {
            opt_compressed = 1;
        } else if ((strcmp(arg, "-A") == 0 || strcmp(arg, "--user-agent") == 0) && i + 1 < argc) {
            opt_user_agent = argv[++i];
        } else if ((strcmp(arg, "-b") == 0 || strcmp(arg, "--cookie") == 0) && i + 1 < argc) {
            opt_cookie = argv[++i];
        } else if ((strcmp(arg, "-c") == 0 || strcmp(arg, "--cookie-jar") == 0) && i + 1 < argc) {
            opt_cookie_jar = argv[++i];
        } else if ((strcmp(arg, "-d") == 0 || strcmp(arg, "--data") == 0) && i + 1 < argc) {
            opt_data = argv[++i];
        } else if ((strcmp(arg, "-F") == 0 || strcmp(arg, "--form") == 0) && i + 1 < argc) {
            opt_form = argv[++i];
        } else if ((strcmp(arg, "-H") == 0 || strcmp(arg, "--header") == 0) && i + 1 < argc) {
            if (opt_header_count < 32) opt_headers[opt_header_count++] = argv[++i];
        } else if ((strcmp(arg, "-m") == 0 || strcmp(arg, "--max-time") == 0) && i + 1 < argc) {
            opt_max_time = atoi(argv[++i]);
        } else if ((strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) && i + 1 < argc) {
            opt_output = argv[++i];
        } else if ((strcmp(arg, "-T") == 0 || strcmp(arg, "--upload-file") == 0) && i + 1 < argc) {
            opt_upload_file = argv[++i];
        } else if ((strcmp(arg, "-u") == 0 || strcmp(arg, "--user") == 0) && i + 1 < argc) {
            opt_user = argv[++i];
        } else if ((strcmp(arg, "-w") == 0 || strcmp(arg, "--write-out") == 0) && i + 1 < argc) {
            opt_write_out = argv[++i];
        } else if ((strcmp(arg, "-x") == 0 || strcmp(arg, "--proxy") == 0) && i + 1 < argc) {
            opt_proxy = argv[++i];
        } else if (strncmp(arg, "--limit-rate", 12) == 0 && i + 1 < argc) {
            opt_limit_rate = argv[++i];
        } else if (arg[0] != '-') {
            url = arg;
        }
    }

    if (!url) {
        if (!opt_silent || opt_show_error) puts("curl: no URL specified!");
        return 2;
    }

    return fetch_url(url, url);
}
