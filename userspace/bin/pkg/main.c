#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#define PKG_VERSION "1.1.0"
#define DEFAULT_CONF_PATH "/etc/pkg.conf"
#define DEFAULT_MIRROR "http://mirror.zyqu.pl/akaos"
#define DEFAULT_CACHEDIR "/var/cache/pkg"
#define DEFAULT_DBDIR "/var/lib/pkg"

#define TAR_BLOCK_SIZE 512
#define MAX_PACKAGES 256
#define MAX_LINE 2048
#define HTTP_TIMEOUT_MS 15000
#define MAX_RECURSION_DEPTH 32
#define MAX_INSTALL_QUEUE 256

/* POSIX ustar header */
struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

struct pkg_info {
    char name[64];
    char version[32];
    char arch[32];
    char filename[256];
    char sha256[65];
    char description[256];
    char dependencies[256];
    size_t size;
};

struct config {
    char mirror[256];
    char arch[32];
    char cachedir[128];
    char dbdir[128];
};

/* --- SHA-256 Implementation --- */

struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

static uint32_t ror(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t data[64]) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t W[64];
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(W[i - 15], 7) ^ ror(W[i - 15], 18) ^ (W[i - 15] >> 3);
        uint32_t s1 = ror(W[i - 2], 17) ^ ror(W[i - 2], 19) ^ (W[i - 2] >> 10);
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + W[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t buf_used = (size_t)(ctx->count & 63);
    ctx->count += len;

    if (buf_used > 0) {
        size_t to_fill = 64 - buf_used;
        if (len < to_fill) {
            memcpy(&ctx->buffer[buf_used], p, len);
            return;
        }
        memcpy(&ctx->buffer[buf_used], p, to_fill);
        sha256_transform(ctx, ctx->buffer);
        p += to_fill;
        len -= to_fill;
    }

    while (len >= 64) {
        sha256_transform(ctx, p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        memcpy(ctx->buffer, p, len);
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t digest[32]) {
    size_t buf_used = (size_t)(ctx->count & 63);
    ctx->buffer[buf_used++] = 0x80;

    if (buf_used > 56) {
        memset(&ctx->buffer[buf_used], 0, 64 - buf_used);
        sha256_transform(ctx, ctx->buffer);
        buf_used = 0;
    }
    memset(&ctx->buffer[buf_used], 0, 56 - buf_used);

    uint64_t bit_len = ctx->count * 8;
    for (int i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)(bit_len >> (56 - i * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static int file_sha256(const char *file_path, char out_hex[65]) {
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) return -1;

    struct sha256_ctx ctx;
    sha256_init(&ctx);

    uint8_t buf[2048];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        sha256_update(&ctx, buf, (size_t)n);
    }
    close(fd);

    if (n < 0) return -1;

    uint8_t digest[32];
    sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++) {
        snprintf(out_hex + (i * 2), 3, "%02x", digest[i]);
    }
    out_hex[64] = '\0';
    return 0;
}

/* --- State --- */

static struct config cfg;
static struct pkg_info repo_pkgs[MAX_PACKAGES];
static int repo_pkg_count = 0;

static struct pkg_info installed_pkgs[MAX_PACKAGES];
static int installed_pkg_count = 0;

/* --- Utility Functions --- */

static void trim_whitespace(char *str) {
    if (!str) return;
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[len - 1] = '\0';
        len--;
    }
}

static void ensure_dir_exists(const char *dir) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void ensure_parent_dirs(const char *file_path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    char *last_slash = strrchr(tmp, '/');
    if (last_slash && last_slash != tmp) {
        *last_slash = '\0';
        ensure_dir_exists(tmp);
    }
}

/* Sanitize path: ensures absolute path with no ".." traversal attacks */
static int sanitize_destination_path(const char *raw_name, char *clean_out, size_t out_size) {
    if (!raw_name || raw_name[0] == '\0') return -1;

    char norm[512];
    size_t npos = 0;
    norm[npos++] = '/';

    const char *p = raw_name;
    while (*p == '/') p++;

    while (*p) {
        char seg[128];
        size_t slen = 0;
        while (*p && *p != '/' && slen < sizeof(seg) - 1) {
            seg[slen++] = *p++;
        }
        seg[slen] = '\0';
        while (*p == '/') p++;

        if (strcmp(seg, ".") == 0 || slen == 0) {
            continue;
        } else if (strcmp(seg, "..") == 0) {
            /* Backtrack one segment */
            if (npos > 1) {
                npos--; /* back over '/' */
                while (npos > 1 && norm[npos - 1] != '/') npos--;
            }
        } else {
            if (npos > 1 && norm[npos - 1] != '/') norm[npos++] = '/';
            for (size_t i = 0; i < slen && npos < sizeof(norm) - 1; i++) {
                norm[npos++] = seg[i];
            }
        }
    }
    norm[npos] = '\0';

    if (snprintf(clean_out, out_size, "%s", norm) >= (int)out_size) {
        return -1;
    }
    return 0;
}

/* Compare two version strings: returns 1 if v1 > v2, -1 if v1 < v2, 0 if equal */
static int compare_versions(const char *v1, const char *v2) {
    if (!v1 && !v2) return 0;
    if (!v1) return -1;
    if (!v2) return 1;

    while (*v1 && *v2) {
        if (isdigit((unsigned char)*v1) && isdigit((unsigned char)*v2)) {
            char *end1, *end2;
            long n1 = strtol(v1, &end1, 10);
            long n2 = strtol(v2, &end2, 10);
            if (n1 > n2) return 1;
            if (n1 < n2) return -1;
            v1 = end1;
            v2 = end2;
        } else {
            if (*v1 != *v2) return (*v1 > *v2) ? 1 : -1;
            v1++;
            v2++;
        }
    }
    if (*v1) return 1;
    if (*v2) return -1;
    return 0;
}

/* --- Configuration Loading --- */

static void load_config(void) {
    strncpy(cfg.mirror, DEFAULT_MIRROR, sizeof(cfg.mirror) - 1);
    strncpy(cfg.cachedir, DEFAULT_CACHEDIR, sizeof(cfg.cachedir) - 1);
    strncpy(cfg.dbdir, DEFAULT_DBDIR, sizeof(cfg.dbdir) - 1);

    struct utsname un;
    if (uname(&un) == 0 && strlen(un.machine) > 0) {
        strncpy(cfg.arch, un.machine, sizeof(cfg.arch) - 1);
    } else {
        strncpy(cfg.arch, "x86_64", sizeof(cfg.arch) - 1);
    }

    const char *custom_conf = getenv("PKG_CONF");
    const char *conf_path = (custom_conf && custom_conf[0] != '\0') ? custom_conf : DEFAULT_CONF_PATH;
    FILE *f = fopen(conf_path, "r");
    if (!f) return;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_whitespace(key);
        trim_whitespace(val);

        if (strcmp(key, "mirror") == 0) {
            /* Strip trailing slash from mirror */
            size_t mlen = strlen(val);
            while (mlen > 0 && val[mlen - 1] == '/') val[--mlen] = '\0';
            strncpy(cfg.mirror, val, sizeof(cfg.mirror) - 1);
        } else if (strcmp(key, "arch") == 0) {
            strncpy(cfg.arch, val, sizeof(cfg.arch) - 1);
        } else if (strcmp(key, "cachedir") == 0) {
            strncpy(cfg.cachedir, val, sizeof(cfg.cachedir) - 1);
        } else if (strcmp(key, "dbdir") == 0) {
            strncpy(cfg.dbdir, val, sizeof(cfg.dbdir) - 1);
        }
    }
    fclose(f);
}

/* --- HTTP Download Client --- */

static int http_download(const char *url, const char *dest_file, int verbose) {
    if (strncmp(url, "https://", 8) == 0) {
        fprintf(stderr, "pkg: error: HTTPS is not yet supported. Please configure plain HTTP: %s\n", url);
        return -1;
    }

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;

    char host[128] = {0};
    int port = 80;
    char path[512] = "/";

    int hlen = 0;
    while (*p && *p != ':' && *p != '/' && hlen < (int)sizeof(host) - 1) {
        host[hlen++] = *p++;
    }
    host[hlen] = '\0';

    if (hlen == 0) {
        fprintf(stderr, "pkg: error: invalid URL (empty host): %s\n", url);
        return -1;
    }

    if (*p == ':') {
        p++;
        port = atoi(p);
        while (*p && *p != '/') p++;
        if (port <= 0 || port > 65535) port = 80;
    }

    if (*p == '/') {
        strncpy(path, p, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    if (verbose) {
        printf("pkg: resolving %s...\n", host);
    }

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr) {
        fprintf(stderr, "pkg: error: unable to resolve host '%s'\n", host);
        return -1;
    }

    uint32_t target_ip;
    memcpy(&target_ip, he->h_addr, sizeof(uint32_t));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "pkg: error: socket creation failed\n");
        return -1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = target_ip;

    if (verbose) {
        printf("pkg: connecting to %s:%d...\n", host, port);
    }

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "pkg: error: failed to connect to %s:%d\n", host, port);
        close(sock);
        return -1;
    }

    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: akaOS-pkg/%s (%s)\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path, host, PKG_VERSION, cfg.arch);

    if (send(sock, request, strlen(request), 0) < 0) {
        fprintf(stderr, "pkg: error: failed to send HTTP request\n");
        close(sock);
        return -1;
    }

    /* Download to temporary file first to guarantee atomic write */
    char temp_dest[512];
    snprintf(temp_dest, sizeof(temp_dest), "%s.part", dest_file);

    ensure_parent_dirs(temp_dest);
    int out_fd = open(temp_dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        fprintf(stderr, "pkg: error: cannot open temporary file '%s' for writing\n", temp_dest);
        close(sock);
        return -1;
    }

    char buffer[2048];
    int in_body = 0;
    int header_pos = 0;
    char header_buf[4096];
    int http_status = 0;
    size_t total_downloaded = 0;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, HTTP_TIMEOUT_MS);
        if (pr <= 0 || !(pfd.revents & POLLIN)) break;

        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;

        if (in_body) {
            write(out_fd, buffer, (size_t)n);
            total_downloaded += (size_t)n;
        } else {
            for (ssize_t i = 0; i < n; i++) {
                if (in_body) {
                    write(out_fd, &buffer[i], 1);
                    total_downloaded++;
                } else {
                    if (header_pos < (int)sizeof(header_buf) - 1) {
                        header_buf[header_pos++] = buffer[i];
                        header_buf[header_pos] = '\0';
                        if (header_pos >= 4 &&
                            header_buf[header_pos - 4] == '\r' &&
                            header_buf[header_pos - 3] == '\n' &&
                            header_buf[header_pos - 2] == '\r' &&
                            header_buf[header_pos - 1] == '\n') {
                            in_body = 1;
                            char *space = strchr(header_buf, ' ');
                            if (space) {
                                http_status = atoi(space + 1);
                            }
                        }
                    } else {
                        in_body = 1;
                        write(out_fd, &buffer[i], 1);
                        total_downloaded++;
                    }
                }
            }
        }
    }

    close(out_fd);
    close(sock);

    if (http_status >= 400 || http_status == 0) {
        fprintf(stderr, "pkg: HTTP error %d while downloading %s\n", http_status, path);
        unlink(temp_dest);
        return -1;
    }

    /* Atomic move into final location */
    if (rename(temp_dest, dest_file) != 0) {
        /* Fallback if rename cross-device */
        unlink(dest_file);
        if (rename(temp_dest, dest_file) != 0) {
            fprintf(stderr, "pkg: error: cannot finalize %s\n", dest_file);
            unlink(temp_dest);
            return -1;
        }
    }

    if (verbose) {
        printf("pkg: downloaded %zu bytes -> %s\n", total_downloaded, dest_file);
    }

    return 0;
}

/* --- Database Parsing --- */

static void load_installed_database(void) {
    installed_pkg_count = 0;
    char path[256];
    snprintf(path, sizeof(path), "%s/installed.db", cfg.dbdir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f) && installed_pkg_count < MAX_PACKAGES) {
        trim_whitespace(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char name[64] = {0};
        char ver[32] = {0};
        char arch[32] = {0};
        if (sscanf(line, "%63s %31s %31s", name, ver, arch) >= 2) {
            strncpy(installed_pkgs[installed_pkg_count].name, name, 63);
            strncpy(installed_pkgs[installed_pkg_count].version, ver, 31);
            strncpy(installed_pkgs[installed_pkg_count].arch, arch[0] ? arch : cfg.arch, 31);
            installed_pkg_count++;
        }
    }
    fclose(f);
}

static void save_installed_database(void) {
    ensure_dir_exists(cfg.dbdir);
    char path[256];
    char temp_path[256];
    snprintf(path, sizeof(path), "%s/installed.db", cfg.dbdir);
    snprintf(temp_path, sizeof(temp_path), "%s/installed.db.tmp", cfg.dbdir);

    FILE *f = fopen(temp_path, "w");
    if (!f) {
        fprintf(stderr, "pkg: error: cannot write to %s\n", temp_path);
        return;
    }

    fprintf(f, "# akaOS installed packages database\n");
    for (int i = 0; i < installed_pkg_count; i++) {
        fprintf(f, "%s %s %s\n",
                installed_pkgs[i].name,
                installed_pkgs[i].version,
                installed_pkgs[i].arch[0] ? installed_pkgs[i].arch : cfg.arch);
    }
    fclose(f);

    rename(temp_path, path);
}

static struct pkg_info *find_installed_pkg(const char *name) {
    for (int i = 0; i < installed_pkg_count; i++) {
        if (strcmp(installed_pkgs[i].name, name) == 0) {
            return &installed_pkgs[i];
        }
    }
    return NULL;
}

static void load_repo_database(void) {
    repo_pkg_count = 0;
    char path[256];
    snprintf(path, sizeof(path), "%s/packages.db", cfg.dbdir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_LINE];
    struct pkg_info current;
    memset(&current, 0, sizeof(current));
    int in_entry = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        if (line[0] == '#' || line[0] == '\0') {
            if (in_entry && current.name[0] != '\0' && repo_pkg_count < MAX_PACKAGES) {
                repo_pkgs[repo_pkg_count++] = current;
                memset(&current, 0, sizeof(current));
                in_entry = 0;
            }
            continue;
        }

        in_entry = 1;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = line;
        char *val = colon + 1;
        trim_whitespace(key);
        trim_whitespace(val);

        if (strcasecmp(key, "Package") == 0) {
            strncpy(current.name, val, sizeof(current.name) - 1);
        } else if (strcasecmp(key, "Version") == 0) {
            strncpy(current.version, val, sizeof(current.version) - 1);
        } else if (strcasecmp(key, "Architecture") == 0 || strcasecmp(key, "Arch") == 0) {
            strncpy(current.arch, val, sizeof(current.arch) - 1);
        } else if (strcasecmp(key, "Filename") == 0) {
            strncpy(current.filename, val, sizeof(current.filename) - 1);
        } else if (strcasecmp(key, "SHA256") == 0) {
            strncpy(current.sha256, val, sizeof(current.sha256) - 1);
        } else if (strcasecmp(key, "Description") == 0) {
            strncpy(current.description, val, sizeof(current.description) - 1);
        } else if (strcasecmp(key, "Dependencies") == 0 || strcasecmp(key, "Depends") == 0) {
            strncpy(current.dependencies, val, sizeof(current.dependencies) - 1);
        } else if (strcasecmp(key, "Size") == 0) {
            current.size = (size_t)strtoull(val, NULL, 10);
        }
    }

    if (in_entry && current.name[0] != '\0' && repo_pkg_count < MAX_PACKAGES) {
        repo_pkgs[repo_pkg_count++] = current;
    }

    fclose(f);
}

static struct pkg_info *find_repo_pkg(const char *name) {
    struct pkg_info *best = NULL;
    for (int i = 0; i < repo_pkg_count; i++) {
        if (strcmp(repo_pkgs[i].name, name) == 0) {
            if (!best || compare_versions(repo_pkgs[i].version, best->version) > 0) {
                best = &repo_pkgs[i];
            }
        }
    }
    return best;
}

/* --- Package Extraction (.akp / ustar tar) with Hooks & Checksums --- */

static uint64_t parse_octal(const char *src, size_t size) {
    uint64_t val = 0;
    while (size > 0 && (*src == ' ' || *src == '\0')) { src++; size--; }
    while (size > 0 && *src >= '0' && *src <= '7') {
        val = (val << 3) + (*src - '0');
        src++;
        size--;
    }
    return val;
}

static int extract_package_archive(const char *archive_path, const char *pkg_name) {
    int fd = open(archive_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "pkg: error: cannot open package archive %s\n", archive_path);
        return -1;
    }

    char filelist_path[256];
    char temp_list_path[256];
    snprintf(filelist_path, sizeof(filelist_path), "%s/files", cfg.dbdir);
    ensure_dir_exists(filelist_path);
    snprintf(filelist_path, sizeof(filelist_path), "%s/files/%s.list", cfg.dbdir, pkg_name);
    snprintf(temp_list_path, sizeof(temp_list_path), "%s/files/%s.list.tmp", cfg.dbdir, pkg_name);

    char scripts_dir[256];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", cfg.dbdir);
    ensure_dir_exists(scripts_dir);

    char postinst_path[512] = {0};
    char prerm_path[512] = {0};

    FILE *list_file = fopen(temp_list_path, "w");
    if (!list_file) {
        fprintf(stderr, "pkg: warning: cannot create filelist %s\n", temp_list_path);
    }

    uint8_t block[TAR_BLOCK_SIZE];
    int files_extracted = 0;

    for (;;) {
        ssize_t n = read(fd, block, TAR_BLOCK_SIZE);
        if (n <= 0) break;
        if (n < TAR_BLOCK_SIZE) break;

        /* Check for end of tar archive */
        int all_zero = 1;
        for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
            if (block[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero) break;

        struct ustar_header *hdr = (struct ustar_header *)block;
        char name[256] = {0};
        if (hdr->prefix[0]) {
            snprintf(name, sizeof(name), "%s/%s", hdr->prefix, hdr->name);
        } else {
            strncpy(name, hdr->name, sizeof(name) - 1);
        }

        uint64_t size = parse_octal(hdr->size, sizeof(hdr->size));
        mode_t mode = (mode_t)parse_octal(hdr->mode, sizeof(hdr->mode));
        if (mode == 0) mode = 0755;

        /* Skip .PKGINFO */
        if (strcmp(name, ".PKGINFO") == 0) {
            size_t blocks_to_skip = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            for (size_t b = 0; b < blocks_to_skip; b++) {
                read(fd, block, TAR_BLOCK_SIZE);
            }
            continue;
        }

        /* Handle package control scripts */
        if (strcmp(name, "CONTROL/postinst") == 0) {
            snprintf(postinst_path, sizeof(postinst_path), "%s/%s.postinst", scripts_dir, pkg_name);
            int pfd = open(postinst_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
            uint64_t rem = size;
            while (rem > 0) {
                uint8_t d[TAR_BLOCK_SIZE];
                read(fd, d, TAR_BLOCK_SIZE);
                size_t tw = rem > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : (size_t)rem;
                if (pfd >= 0) write(pfd, d, tw);
                rem -= (rem > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : rem);
            }
            if (pfd >= 0) close(pfd);
            continue;
        }

        if (strcmp(name, "CONTROL/prerm") == 0) {
            snprintf(prerm_path, sizeof(prerm_path), "%s/%s.prerm", scripts_dir, pkg_name);
            int rfd = open(prerm_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
            uint64_t rem = size;
            while (rem > 0) {
                uint8_t d[TAR_BLOCK_SIZE];
                read(fd, d, TAR_BLOCK_SIZE);
                size_t tw = rem > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : (size_t)rem;
                if (rfd >= 0) write(rfd, d, tw);
                rem -= (rem > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : rem);
            }
            if (rfd >= 0) close(rfd);
            continue;
        }

        /* Sanitize destination path */
        char dest_path[512];
        const char *pkg_root = getenv("PKG_ROOT");
        char rel_clean[512];
        if (sanitize_destination_path(name, rel_clean, sizeof(rel_clean)) != 0) {
            fprintf(stderr, "pkg: warning: skipping invalid path %s\n", name);
            size_t blocks_to_skip = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            for (size_t b = 0; b < blocks_to_skip; b++) {
                read(fd, block, TAR_BLOCK_SIZE);
            }
            continue;
        }
        if (pkg_root && pkg_root[0] != '\0') {
            snprintf(dest_path, sizeof(dest_path), "%s%s", pkg_root, rel_clean);
        } else {
            snprintf(dest_path, sizeof(dest_path), "%s", rel_clean);
        }

        if (hdr->typeflag == '5' || (hdr->typeflag == '0' && dest_path[strlen(dest_path) - 1] == '/')) {
            /* Directory */
            ensure_dir_exists(dest_path);
        } else {
            /* Regular file */
            ensure_parent_dirs(dest_path);
            int out = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (out >= 0) {
                uint64_t remaining = size;
                while (remaining > 0) {
                    uint8_t data[TAR_BLOCK_SIZE];
                    ssize_t dr = read(fd, data, TAR_BLOCK_SIZE);
                    if (dr <= 0) break;
                    size_t to_write = remaining > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : (size_t)remaining;
                    write(out, data, to_write);
                    remaining -= (remaining > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : remaining);
                }
                close(out);
                files_extracted++;
                if (list_file) {
                    fprintf(list_file, "%s\n", dest_path);
                }
            } else {
                fprintf(stderr, "pkg: warning: failed to write to %s\n", dest_path);
                size_t blocks_to_skip = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
                for (size_t b = 0; b < blocks_to_skip; b++) {
                    read(fd, block, TAR_BLOCK_SIZE);
                }
            }
        }
    }

    if (list_file) fclose(list_file);
    close(fd);

    rename(temp_list_path, filelist_path);

    /* Execute post-installation script if present */
    if (postinst_path[0] && access(postinst_path, X_OK) == 0) {
        printf("[pkg] Executing post-installation script for %s...\n", pkg_name);
        system(postinst_path);
    }

    return files_extracted;
}

/* --- Dependency Resolution --- */

static int is_pkg_queued(const char *name, char queue[][64], int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(queue[i], name) == 0) return 1;
    }
    return 0;
}

static int resolve_dependencies(const char *pkg_name, char queue[][64], int *queue_count, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        fprintf(stderr, "pkg: error: circular dependency detected involving '%s'\n", pkg_name);
        return -1;
    }

    struct pkg_info *pkg = find_repo_pkg(pkg_name);
    if (!pkg) {
        /* If already installed, it's satisfied */
        if (find_installed_pkg(pkg_name)) return 0;
        fprintf(stderr, "pkg: error: missing dependency '%s' not found in repository\n", pkg_name);
        return -1;
    }

    if (pkg->dependencies[0] != '\0') {
        char deps_copy[256];
        strncpy(deps_copy, pkg->dependencies, sizeof(deps_copy) - 1);
        deps_copy[sizeof(deps_copy) - 1] = '\0';

        char *token = strtok(deps_copy, ", ");
        while (token) {
            trim_whitespace(token);
            if (token[0] != '\0') {
                /* If dependency is not yet installed or queued, resolve it first */
                if (!find_installed_pkg(token) && !is_pkg_queued(token, queue, *queue_count)) {
                    if (resolve_dependencies(token, queue, queue_count, depth + 1) != 0) {
                        return -1;
                    }
                }
            }
            token = strtok(NULL, ", ");
        }
    }

    if (!is_pkg_queued(pkg_name, queue, *queue_count)) {
        if (*queue_count >= MAX_INSTALL_QUEUE) {
            fprintf(stderr, "pkg: error: install queue overflow\n");
            return -1;
        }
        strncpy(queue[*queue_count], pkg_name, 63);
        queue[*queue_count][63] = '\0';
        (*queue_count)++;
    }
    return 0;
}

/* --- Commands --- */

static int cmd_update(void) {
    printf("[pkg] Updating repository metadata from %s (%s)...\n", cfg.mirror, cfg.arch);

    char url[1024];
    snprintf(url, sizeof(url), "%s/%s/packages.db.txt", cfg.mirror, cfg.arch);

    char dest[256];
    ensure_dir_exists(cfg.dbdir);
    snprintf(dest, sizeof(dest), "%s/packages.db", cfg.dbdir);

    if (http_download(url, dest, 0) != 0) {
        fprintf(stderr, "[pkg] Failed to update repository database from %s\n", url);
        return 1;
    }

    load_repo_database();
    printf("[pkg] Metadata synchronized successfully. (%d packages available)\n", repo_pkg_count);
    return 0;
}

static int install_single_package(const char *pkg_name) {
    struct pkg_info *pkg = find_repo_pkg(pkg_name);
    if (!pkg) {
        fprintf(stderr, "[pkg] error: package '%s' not found in repository.\n", pkg_name);
        return 1;
    }

    struct pkg_info *inst = find_installed_pkg(pkg_name);
    if (inst) {
        if (compare_versions(inst->version, pkg->version) >= 0) {
            printf("[pkg] %s is already up to date (%s).\n", pkg_name, inst->version);
            return 0;
        }
        printf("[pkg] Upgrading %s (%s -> %s)...\n", pkg_name, inst->version, pkg->version);
    } else {
        printf("[pkg] Installing %s (%s) for %s...\n", pkg_name, pkg->version, cfg.arch);
    }

    char url[1024];
    if (pkg->filename[0] == '/') {
        snprintf(url, sizeof(url), "%s/%s%s", cfg.mirror, cfg.arch, pkg->filename);
    } else {
        snprintf(url, sizeof(url), "%s/%s/%s", cfg.mirror, cfg.arch, pkg->filename);
    }

    ensure_dir_exists(cfg.cachedir);
    char cached_file[256];
    snprintf(cached_file, sizeof(cached_file), "%s/%s-%s.akp", cfg.cachedir, pkg->name, pkg->version);

    /* Only download if file not in cache or checksum mismatch */
    int need_download = 1;
    if (access(cached_file, R_OK) == 0 && pkg->sha256[0] != '\0') {
        char local_sha[65];
        if (file_sha256(cached_file, local_sha) == 0 && strcasecmp(local_sha, pkg->sha256) == 0) {
            need_download = 0;
            printf("[pkg] Using cached archive %s\n", cached_file);
        }
    }

    if (need_download) {
        printf("[pkg] Fetching %s...\n", url);
        if (http_download(url, cached_file, 0) != 0) {
            fprintf(stderr, "[pkg] error: failed to download package archive %s\n", url);
            return 1;
        }
    }

    /* Verify SHA-256 checksum if provided by repository */
    if (pkg->sha256[0] != '\0') {
        char actual_sha[65];
        if (file_sha256(cached_file, actual_sha) != 0) {
            fprintf(stderr, "[pkg] error: failed to calculate checksum of %s\n", cached_file);
            return 1;
        }
        if (strcasecmp(actual_sha, pkg->sha256) != 0) {
            fprintf(stderr, "[pkg] error: SHA-256 mismatch for %s!\n", pkg->name);
            fprintf(stderr, "      Expected: %s\n", pkg->sha256);
            fprintf(stderr, "      Actual:   %s\n", actual_sha);
            unlink(cached_file);
            return 1;
        }
    }

    printf("[pkg] Unpacking %s...\n", pkg->name);
    int extracted = extract_package_archive(cached_file, pkg->name);
    if (extracted < 0) {
        fprintf(stderr, "[pkg] error: failed to unpack %s\n", cached_file);
        return 1;
    }

    /* Update installed database */
    inst = find_installed_pkg(pkg_name);
    if (inst) {
        strncpy(inst->version, pkg->version, sizeof(inst->version) - 1);
    } else if (installed_pkg_count < MAX_PACKAGES) {
        strncpy(installed_pkgs[installed_pkg_count].name, pkg->name, sizeof(installed_pkgs[installed_pkg_count].name) - 1);
        strncpy(installed_pkgs[installed_pkg_count].version, pkg->version, sizeof(installed_pkgs[installed_pkg_count].version) - 1);
        strncpy(installed_pkgs[installed_pkg_count].arch, cfg.arch, sizeof(installed_pkgs[installed_pkg_count].arch) - 1);
        installed_pkg_count++;
    }
    save_installed_database();

    printf("[pkg] Successfully installed %s (%s) [%d files extracted]\n", pkg->name, pkg->version, extracted);
    return 0;
}

static int cmd_install(const char *target_pkg) {
    load_repo_database();
    load_installed_database();

    if (repo_pkg_count == 0) {
        fprintf(stderr, "[pkg] Repository database is empty. Please run 'pkg update' first.\n");
        return 1;
    }

    char queue[MAX_INSTALL_QUEUE][64];
    int queue_count = 0;

    if (resolve_dependencies(target_pkg, queue, &queue_count, 0) != 0) {
        return 1;
    }

    if (queue_count > 1) {
        printf("[pkg] Resolved dependencies for %s (%d packages to install):\n", target_pkg, queue_count);
        for (int i = 0; i < queue_count; i++) {
            printf("  - %s\n", queue[i]);
        }
    }

    for (int i = 0; i < queue_count; i++) {
        if (install_single_package(queue[i]) != 0) {
            fprintf(stderr, "[pkg] Failed while installing %s\n", queue[i]);
            return 1;
        }
    }
    return 0;
}

static int cmd_upgrade(void) {
    load_repo_database();
    load_installed_database();

    if (installed_pkg_count == 0) {
        printf("[pkg] No packages currently installed.\n");
        return 0;
    }

    int upgrade_count = 0;
    int success_count = 0;

    for (int i = 0; i < installed_pkg_count; i++) {
        struct pkg_info *cur = &installed_pkgs[i];
        struct pkg_info *repo = find_repo_pkg(cur->name);
        if (repo && compare_versions(repo->version, cur->version) > 0) {
            upgrade_count++;
            printf("[pkg] Upgrading %s: %s -> %s\n", cur->name, cur->version, repo->version);
            if (cmd_install(cur->name) == 0) {
                success_count++;
            }
        }
    }

    if (upgrade_count == 0) {
        printf("[pkg] All installed packages are already up to date.\n");
    } else {
        printf("[pkg] Upgrade finished: %d of %d package(s) updated.\n", success_count, upgrade_count);
    }

    return 0;
}

static int cmd_remove(const char *pkg_name) {
    load_installed_database();

    int found_idx = -1;
    for (int i = 0; i < installed_pkg_count; i++) {
        if (strcmp(installed_pkgs[i].name, pkg_name) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) {
        fprintf(stderr, "[pkg] error: package '%s' is not installed.\n", pkg_name);
        return 1;
    }

    /* Execute pre-removal hook if present */
    char prerm_path[512];
    snprintf(prerm_path, sizeof(prerm_path), "%s/scripts/%s.prerm", cfg.dbdir, pkg_name);
    if (access(prerm_path, X_OK) == 0) {
        printf("[pkg] Running pre-removal script for %s...\n", pkg_name);
        system(prerm_path);
        unlink(prerm_path);
    }
    char postinst_path[512];
    snprintf(postinst_path, sizeof(postinst_path), "%s/scripts/%s.postinst", cfg.dbdir, pkg_name);
    unlink(postinst_path);

    printf("[pkg] Removing package %s...\n", pkg_name);

    char filelist_path[256];
    snprintf(filelist_path, sizeof(filelist_path), "%s/files/%s.list", cfg.dbdir, pkg_name);

    FILE *f = fopen(filelist_path, "r");
    int files_removed = 0;
    if (f) {
        char path[512];
        while (fgets(path, sizeof(path), f)) {
            trim_whitespace(path);
            if (path[0] == '/' && unlink(path) == 0) {
                files_removed++;
            }
        }
        fclose(f);
        unlink(filelist_path);
    }

    /* Remove from installed database */
    for (int i = found_idx; i < installed_pkg_count - 1; i++) {
        installed_pkgs[i] = installed_pkgs[i + 1];
    }
    installed_pkg_count--;
    save_installed_database();

    printf("[pkg] Removed %s (%d files deleted).\n", pkg_name, files_removed);
    return 0;
}

static int cmd_list(void) {
    load_installed_database();
    if (installed_pkg_count == 0) {
        printf("No packages installed.\n");
        return 0;
    }

    printf("%-20s %-16s %-10s\n", "PACKAGE", "VERSION", "ARCH");
    printf("%-20s %-16s %-10s\n", "-------", "-------", "----");
    for (int i = 0; i < installed_pkg_count; i++) {
        printf("%-20s %-16s %-10s\n",
               installed_pkgs[i].name,
               installed_pkgs[i].version,
               installed_pkgs[i].arch);
    }
    printf("\nTotal installed packages: %d\n", installed_pkg_count);
    return 0;
}

static int cmd_search(const char *keyword) {
    load_repo_database();
    if (repo_pkg_count == 0) {
        printf("[pkg] No packages in database. Run 'pkg update' first.\n");
        return 0;
    }

    int matches = 0;
    printf("%-20s %-14s %s\n", "PACKAGE", "VERSION", "DESCRIPTION");
    printf("%-20s %-14s %s\n", "-------", "-------", "-----------");

    for (int i = 0; i < repo_pkg_count; i++) {
        if (!keyword || keyword[0] == '\0' ||
            strstr(repo_pkgs[i].name, keyword) != NULL ||
            strstr(repo_pkgs[i].description, keyword) != NULL) {
            printf("%-20s %-14s %s\n",
                   repo_pkgs[i].name,
                   repo_pkgs[i].version,
                   repo_pkgs[i].description[0] ? repo_pkgs[i].description : "-");
            matches++;
        }
    }

    printf("\nFound %d package(s).\n", matches);
    return 0;
}

static int cmd_info(const char *pkg_name) {
    load_repo_database();
    load_installed_database();

    struct pkg_info *repo = find_repo_pkg(pkg_name);
    struct pkg_info *inst = find_installed_pkg(pkg_name);

    if (!repo && !inst) {
        fprintf(stderr, "[pkg] Package '%s' not found.\n", pkg_name);
        return 1;
    }

    printf("Package:      %s\n", pkg_name);
    if (inst) {
        printf("Status:       installed (version %s)\n", inst->version);
    } else {
        printf("Status:       not installed\n");
    }

    if (repo) {
        printf("Repo Version: %s\n", repo->version);
        printf("Architecture: %s\n", repo->arch);
        printf("Size:         %zu bytes\n", repo->size);
        printf("SHA256:       %s\n", repo->sha256[0] ? repo->sha256 : "none");
        printf("Filename:     %s\n", repo->filename);
        printf("Dependencies: %s\n", repo->dependencies[0] ? repo->dependencies : "none");
        printf("Description:  %s\n", repo->description[0] ? repo->description : "none");
    }

    return 0;
}

static void print_help(void) {
    printf("akaOS Package Manager (pkg) v%s\n\n"
           "Usage: pkg <command> [options...]\n\n"
           "Commands:\n"
           "  update                Refresh package lists from mirror (%s)\n"
           "  install <pkg>         Download and install a package (and dependencies)\n"
           "  upgrade               Upgrade all installed packages to newest versions\n"
           "  remove <pkg>          Uninstall a package and delete its files\n"
           "  list                  List all currently installed packages\n"
           "  search <keyword>      Search repository for available packages\n"
           "  info <pkg>            Display details about a package\n"
           "  help                  Show this help text\n",
           PKG_VERSION, cfg.mirror);
}

int main(int argc, char **argv) {
    load_config();

    if (argc < 2) {
        print_help();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "update") == 0) {
        return cmd_update();
    } else if (strcmp(cmd, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "pkg: missing package name.\nUsage: pkg install <package>\n");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            if (cmd_install(argv[i]) != 0) return 1;
        }
        return 0;
    } else if (strcmp(cmd, "upgrade") == 0) {
        return cmd_upgrade();
    } else if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "uninstall") == 0) {
        if (argc < 3) {
            fprintf(stderr, "pkg: missing package name.\nUsage: pkg remove <package>\n");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            if (cmd_remove(argv[i]) != 0) return 1;
        }
        return 0;
    } else if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    } else if (strcmp(cmd, "search") == 0) {
        return cmd_search(argc >= 3 ? argv[2] : NULL);
    } else if (strcmp(cmd, "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "pkg: missing package name.\nUsage: pkg info <package>\n");
            return 1;
        }
        return cmd_info(argv[2]);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help();
        return 0;
    } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("pkg %s (akaOS)\n", PKG_VERSION);
        return 0;
    } else {
        fprintf(stderr, "pkg: unknown command '%s'. Run 'pkg help' for usage.\n", cmd);
        return 1;
    }
}
