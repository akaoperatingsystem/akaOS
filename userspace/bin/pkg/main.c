#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#define PKG_VERSION "1.0.0"
#define DEFAULT_CONF_PATH "/etc/pkg.conf"
#define DEFAULT_MIRROR "http://mirror.zyqu.pl/akaos"
#define DEFAULT_CACHEDIR "/var/cache/pkg"
#define DEFAULT_DBDIR "/var/lib/pkg"

#define TAR_BLOCK_SIZE 512
#define MAX_PACKAGES 1024
#define MAX_LINE 1024
#define HTTP_TIMEOUT_MS 10000

/* POSIX ustar header structure */
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

static struct config cfg;
static struct pkg_info repo_pkgs[MAX_PACKAGES];
static int repo_pkg_count = 0;

static struct pkg_info installed_pkgs[MAX_PACKAGES];
static int installed_pkg_count = 0;

/* --- Utility Functions --- */

static void trim_whitespace(char *str) {
    if (!str) return;
    char *start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[len - 1] = '\0';
        len--;
    }
}

static void ensure_dir_exists(const char *dir) {
    char tmp[256];
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

/* Compare two version strings: returns 1 if v1 > v2, -1 if v1 < v2, 0 if equal */
static int compare_versions(const char *v1, const char *v2) {
    while (*v1 && *v2) {
        if (*v1 >= '0' && *v1 <= '9' && *v2 >= '0' && *v2 <= '9') {
            long n1 = strtol(v1, (char **)&v1, 10);
            long n2 = strtol(v2, (char **)&v2, 10);
            if (n1 > n2) return 1;
            if (n1 < n2) return -1;
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

    FILE *f = fopen(DEFAULT_CONF_PATH, "r");
    if (!f) return;

    char line[256];
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
        fprintf(stderr, "pkg: error: HTTPS is not yet supported. Please use plain HTTP: %s\n", url);
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

    ensure_parent_dirs(dest_file);
    int out_fd = open(dest_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        fprintf(stderr, "pkg: error: cannot open destination file '%s' for writing\n", dest_file);
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
                            /* Parse HTTP response status code */
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
        unlink(dest_file);
        return -1;
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
    snprintf(path, sizeof(path), "%s/installed.db", cfg.dbdir);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "pkg: error: cannot write to %s\n", path);
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
    for (int i = 0; i < repo_pkg_count; i++) {
        if (strcmp(repo_pkgs[i].name, name) == 0) {
            return &repo_pkgs[i];
        }
    }
    return NULL;
}

/* --- Package Extraction (.akp / ustar tar) --- */

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
    snprintf(filelist_path, sizeof(filelist_path), "%s/files", cfg.dbdir);
    ensure_dir_exists(filelist_path);
    snprintf(filelist_path, sizeof(filelist_path), "%s/files/%s.list", cfg.dbdir, pkg_name);

    FILE *list_file = fopen(filelist_path, "w");
    if (!list_file) {
        fprintf(stderr, "pkg: warning: cannot create filelist %s\n", filelist_path);
    }

    uint8_t block[TAR_BLOCK_SIZE];
    int files_extracted = 0;

    for (;;) {
        ssize_t n = read(fd, block, TAR_BLOCK_SIZE);
        if (n <= 0) break;
        if (n < TAR_BLOCK_SIZE) break;

        /* Check for end of tar archive (two consecutive zero blocks) */
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

        /* Skip metadata control files from being extracted to root directly */
        if (strcmp(name, ".PKGINFO") == 0 || strncmp(name, "CONTROL/", 8) == 0) {
            size_t blocks_to_skip = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            for (size_t b = 0; b < blocks_to_skip; b++) {
                read(fd, block, TAR_BLOCK_SIZE);
            }
            continue;
        }

        /* Build destination target path starting with '/' */
        char dest_path[512];
        if (name[0] == '/') {
            snprintf(dest_path, sizeof(dest_path), "%s", name);
        } else {
            snprintf(dest_path, sizeof(dest_path), "/%s", name);
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
                fprintf(stderr, "pkg: warning: failed to extract %s\n", dest_path);
                size_t blocks_to_skip = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
                for (size_t b = 0; b < blocks_to_skip; b++) {
                    read(fd, block, TAR_BLOCK_SIZE);
                }
            }
        }
    }

    if (list_file) fclose(list_file);
    close(fd);
    return files_extracted;
}

/* --- Commands --- */

static int cmd_update(void) {
    printf("[pkg] Updating repository metadata from %s (%s)...\n", cfg.mirror, cfg.arch);

    char url[512];
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

static int cmd_install(const char *pkg_name) {
    load_repo_database();
    load_installed_database();

    struct pkg_info *pkg = find_repo_pkg(pkg_name);
    if (!pkg) {
        fprintf(stderr, "[pkg] error: package '%s' not found in repository.\n", pkg_name);
        fprintf(stderr, "[pkg] Try running 'pkg update' first to refresh package database.\n");
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

    char url[512];
    if (pkg->filename[0] == '/') {
        snprintf(url, sizeof(url), "%s/%s%s", cfg.mirror, cfg.arch, pkg->filename);
    } else {
        snprintf(url, sizeof(url), "%s/%s/%s", cfg.mirror, cfg.arch, pkg->filename);
    }

    ensure_dir_exists(cfg.cachedir);
    char cached_file[256];
    snprintf(cached_file, sizeof(cached_file), "%s/%s-%s.akp", cfg.cachedir, pkg->name, pkg->version);

    printf("[pkg] Fetching %s...\n", url);
    if (http_download(url, cached_file, 0) != 0) {
        fprintf(stderr, "[pkg] error: failed to download package archive %s\n", url);
        return 1;
    }

    printf("[pkg] Unpacking %s...\n", pkg->name);
    int extracted = extract_package_archive(cached_file, pkg->name);
    if (extracted < 0) {
        fprintf(stderr, "[pkg] error: failed to unpack %s\n", cached_file);
        return 1;
    }

    /* Update installed database */
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
           "  install <pkg>         Download and install a package\n"
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
