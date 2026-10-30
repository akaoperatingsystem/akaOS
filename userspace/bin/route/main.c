#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static char file_buf[2048];

static void print_usage(void) {
    puts("Usage: route [-nvee] [-A inet]\n"
         "       route {add|del} [-net|-host] target [netmask Nm] [gw Gw] [[dev] If]\n\n"
         "Options:\n"
         "  -n, --numeric  don't resolve names\n"
         "  -v, --verbose  be verbose\n"
         "  -h, --help     display this help and exit\n"
         "  -V, --version  display version information and exit");
}

static uint32_t hex_to_u32(const char *s, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; ++i) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        val = (val << 4) | d;
    }
    return val;
}

static char *next_token(char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '\0') return NULL;
    char *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    if (**p) {
        **p = '\0';
        (*p)++;
    }
    return start;
}

static int show_routes(int numeric) {
    int fd = open("/proc/net/route", O_RDONLY);
    if (fd < 0) {
        puts("route: cannot open /proc/net/route");
        return 1;
    }
    ssize_t n = read(fd, file_buf, sizeof(file_buf) - 1);
    close(fd);
    if (n <= 0) {
        puts("route: empty /proc/net/route");
        return 1;
    }
    file_buf[n] = '\0';

    puts("Kernel IP routing table");
    puts("Destination     Gateway         Genmask         Flags Metric Ref    Use Iface");

    char *line = file_buf;
    char *nl = strchr(line, '\n');
    if (nl) line = nl + 1;

    while (*line) {
        char *next_nl = strchr(line, '\n');
        if (next_nl) *next_nl = '\0';

        char *p = line;
        char *iface = next_token(&p);
        char *dest_hex = next_token(&p);
        char *gw_hex = next_token(&p);
        char *flags_str = next_token(&p);
        (void)next_token(&p);
        (void)next_token(&p);
        (void)next_token(&p);
        char *mask_hex = next_token(&p);

        if (iface && dest_hex && gw_hex && flags_str && mask_hex) {
            uint32_t dest = hex_to_u32(dest_hex, 8);
            uint32_t gw = hex_to_u32(gw_hex, 8);
            uint32_t mask = hex_to_u32(mask_hex, 8);
            uint32_t flags = hex_to_u32(flags_str, 4);

            char dest_ip[32];
            char gw_ip[32];
            char mask_ip[32];

            if (dest == 0 && !numeric) {
                snprintf(dest_ip, sizeof(dest_ip), "default");
            } else {
                snprintf(dest_ip, sizeof(dest_ip), "%u.%u.%u.%u",
                         dest & 0xff, (dest >> 8) & 0xff, (dest >> 16) & 0xff, (dest >> 24) & 0xff);
            }

            if (gw == 0) {
                snprintf(gw_ip, sizeof(gw_ip), "0.0.0.0");
            } else {
                snprintf(gw_ip, sizeof(gw_ip), "%u.%u.%u.%u",
                         gw & 0xff, (gw >> 8) & 0xff, (gw >> 16) & 0xff, (gw >> 24) & 0xff);
            }

            snprintf(mask_ip, sizeof(mask_ip), "%u.%u.%u.%u",
                     mask & 0xff, (mask >> 8) & 0xff, (mask >> 16) & 0xff, (mask >> 24) & 0xff);

            char f_str[8];
            int fi = 0;
            if (flags & 0x0001) f_str[fi++] = 'U';
            if (flags & 0x0002) f_str[fi++] = 'G';
            if (flags & 0x0004) f_str[fi++] = 'H';
            if (fi == 0) f_str[fi++] = '-';
            f_str[fi] = '\0';

            printf("%-15s %-15s %-15s %-5s %-6d %-6d %5d %s\n",
                   dest_ip, gw_ip, mask_ip, f_str, 0, 0, 0, iface);
        }

        if (!next_nl) break;
        line = next_nl + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int numeric = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            puts("route (akaOS Network Utilities) 1.0");
            return 0;
        }
        if (strcmp(argv[1], "-n") == 0) {
            numeric = 1;
        } else if (strcmp(argv[1], "add") == 0) {
            return 0;
        } else if (strcmp(argv[1], "del") == 0) {
            return 0;
        }
    }

    return show_routes(numeric);
}
