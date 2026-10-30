#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <akaos/net.h>

static char file_buf[2048];
static char local_addr[64];
static char remote_addr[64];

static void print_usage(void) {
    puts("Usage: netstat [options]\n"
         "Print network connections, routing tables, interface statistics, etc.\n\n"
         "Options:\n"
         "  -a, --all         show both listening and non-listening sockets\n"
         "  -l, --listening   show only listening sockets\n"
         "  -t, --tcp         show only TCP sockets\n"
         "  -u, --udp         show only UDP sockets\n"
         "  -n, --numeric     don't resolve names\n"
         "  -r, --route       display routing table\n"
         "  -i, --interfaces  display interface table\n"
         "  -s, --statistics  display networking statistics\n"
         "  -h, --help        display this help and exit\n"
         "  -v, --version     output version information and exit");
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

static int read_proc_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, file_buf, sizeof(file_buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    file_buf[n] = '\0';
    return 1;
}

static void format_hex_addr(const char *hex_ip, uint16_t port, char *out, size_t out_sz, int numeric) {
    (void)numeric;
    uint32_t raw_hex = hex_to_u32(hex_ip, 8);
    unsigned int o1 = (raw_hex >> 0) & 0xff;
    unsigned int o2 = (raw_hex >> 8) & 0xff;
    unsigned int o3 = (raw_hex >> 16) & 0xff;
    unsigned int o4 = (raw_hex >> 24) & 0xff;

    if (raw_hex == 0) {
        if (port == 0) {
            snprintf(out, out_sz, "0.0.0.0:*");
        } else {
            snprintf(out, out_sz, "0.0.0.0:%u", (unsigned int)port);
        }
    } else {
        if (port == 0) {
            snprintf(out, out_sz, "%u.%u.%u.%u:*", o1, o2, o3, o4);
        } else {
            snprintf(out, out_sz, "%u.%u.%u.%u:%u", o1, o2, o3, o4, (unsigned int)port);
        }
    }
}

static const char *tcp_state_name(uint32_t st) {
    switch (st) {
        case 0x01: return "ESTABLISHED";
        case 0x02: return "SYN_SENT";
        case 0x03: return "SYN_RECV";
        case 0x04: return "FIN_WAIT1";
        case 0x05: return "FIN_WAIT2";
        case 0x06: return "TIME_WAIT";
        case 0x07: return "CLOSE";
        case 0x08: return "CLOSE_WAIT";
        case 0x09: return "LAST_ACK";
        case 0x0A: return "LISTEN";
        case 0x0B: return "CLOSING";
        default:   return "UNKNOWN";
    }
}

static void show_sockets(int show_tcp, int show_udp, int all_socks, int listening_only, int numeric) {
    int printed_header = 0;

    if (show_tcp && read_proc_file("/proc/net/tcp")) {
        char *line = file_buf;
        char *nl = strchr(line, '\n');
        if (nl) line = nl + 1;

        while (*line) {
            char *next_nl = strchr(line, '\n');
            if (next_nl) *next_nl = '\0';

            char *p = line;
            while (*p == ' ') p++;
            char *colon = strchr(p, ':');
            if (colon) {
                p = colon + 1;
                while (*p == ' ') p++;
                char *loc = next_token(&p);
                char *rem = next_token(&p);
                char *st_s = next_token(&p);
                char *q_s = next_token(&p);

                if (loc && rem && st_s) {
                    char *loc_col = strchr(loc, ':');
                    char *rem_col = strchr(rem, ':');
                    if (loc_col && rem_col) {
                        *loc_col = '\0';
                        *rem_col = '\0';
                        uint16_t loc_p = (uint16_t)hex_to_u32(loc_col + 1, 4);
                        uint16_t rem_p = (uint16_t)hex_to_u32(rem_col + 1, 4);
                        uint32_t state = hex_to_u32(st_s, 2);

                        int is_listen = (state == 0x0A);
                        if ((!listening_only || is_listen) &&
                            (all_socks || listening_only || !is_listen)) {
                            if (!printed_header) {
                                puts("Active Internet connections (servers and established)");
                                puts("Proto Recv-Q Send-Q Local Address           Foreign Address         State");
                                printed_header = 1;
                            }
                            format_hex_addr(loc, loc_p, local_addr, sizeof(local_addr), numeric);
                            format_hex_addr(rem, rem_p, remote_addr, sizeof(remote_addr), numeric);

                            uint32_t tx_q = 0, rx_q = 0;
                            if (q_s) {
                                char *q_col = strchr(q_s, ':');
                                if (q_col) {
                                    *q_col = '\0';
                                    tx_q = hex_to_u32(q_s, 8);
                                    rx_q = hex_to_u32(q_col + 1, 8);
                                }
                            }

                            printf("tcp   %6u %6u %-23s %-23s %s\n",
                                   rx_q, tx_q, local_addr, remote_addr, tcp_state_name(state));
                        }
                    }
                }
            }

            if (!next_nl) break;
            line = next_nl + 1;
        }
    }

    if (show_udp && read_proc_file("/proc/net/udp")) {
        char *line = file_buf;
        char *nl = strchr(line, '\n');
        if (nl) line = nl + 1;

        while (*line) {
            char *next_nl = strchr(line, '\n');
            if (next_nl) *next_nl = '\0';

            char *p = line;
            while (*p == ' ') p++;
            char *colon = strchr(p, ':');
            if (colon) {
                p = colon + 1;
                while (*p == ' ') p++;
                char *loc = next_token(&p);
                char *rem = next_token(&p);

                if (loc && rem) {
                    char *loc_col = strchr(loc, ':');
                    char *rem_col = strchr(rem, ':');
                    if (loc_col && rem_col) {
                        *loc_col = '\0';
                        *rem_col = '\0';
                        uint16_t loc_p = (uint16_t)hex_to_u32(loc_col + 1, 4);
                        uint16_t rem_p = (uint16_t)hex_to_u32(rem_col + 1, 4);

                        if (!printed_header) {
                            puts("Active Internet connections (servers and established)");
                            puts("Proto Recv-Q Send-Q Local Address           Foreign Address         State");
                            printed_header = 1;
                        }
                        format_hex_addr(loc, loc_p, local_addr, sizeof(local_addr), numeric);
                        format_hex_addr(rem, rem_p, remote_addr, sizeof(remote_addr), numeric);

                        printf("udp        0      0 %-23s %-23s\n",
                               local_addr, remote_addr);
                    }
                }
            }

            if (!next_nl) break;
            line = next_nl + 1;
        }
    }

    if (!printed_header) {
        puts("Active Internet connections (servers and established)");
        puts("Proto Recv-Q Send-Q Local Address           Foreign Address         State");
    }
}

static void show_routing_table(int numeric) {
    if (!read_proc_file("/proc/net/route")) {
        puts("netstat: cannot open /proc/net/route");
        return;
    }

    puts("Kernel IP routing table");
    puts("Destination     Gateway         Genmask         Flags   MSS Window  irtt Iface");

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
        (void)next_token(&p); /* ref */
        (void)next_token(&p); /* use */
        (void)next_token(&p); /* metric */
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

            printf("%-15s %-15s %-15s %-5s %5d %-7d %5d %s\n",
                   dest_ip, gw_ip, mask_ip, f_str, 0, 0, 0, iface);
        }

        if (!next_nl) break;
        line = next_nl + 1;
    }
}

static void show_interfaces(void) {
    puts("Kernel Interface table");
    puts("Iface      MTU    RX-OK RX-ERR RX-DRP RX-OVR    TX-OK TX-ERR TX-DRP TX-OVR Flg");

    struct akaos_net_stats stats;
    memset(&stats, 0, sizeof(stats));
    int fd = open("/dev/net", O_RDWR);
    if (fd >= 0) {
        ioctl(fd, SIOCGIFSTATS, &stats);
        close(fd);
    }

    printf("%-8s %5d %8u %6d %6d %6d %8u %6d %6d %6d %s\n",
           "eth0", 1500, stats.rx_packets, 0, 0, 0, stats.tx_packets, 0, 0, 0, "BMRU");
    printf("          RX packets: %u  TX packets: %u\n", stats.rx_packets, stats.tx_packets);
    printf("%-8s %5d %8u %6d %6d %6d %8u %6d %6d %6d %s\n",
           "lo", 65536, 0, 0, 0, 0, 0, 0, 0, 0, "LU");
}

static void show_statistics(void) {
    struct akaos_net_stats stats;
    memset(&stats, 0, sizeof(stats));
    int fd = open("/dev/net", O_RDWR);
    if (fd >= 0) {
        ioctl(fd, SIOCGIFSTATS, &stats);
        close(fd);
    }
    puts("Ip:");
    printf("    %u total packets received\n", stats.rx_packets);
    printf("    RX packets: %u, TX packets: %u (RX bytes: %u, TX bytes: %u)\n",
           stats.rx_packets, stats.tx_packets, stats.rx_bytes, stats.tx_bytes);
}

int main(int argc, char **argv) {
    int show_all = 0;
    int show_listen = 0;
    int show_tcp = 0;
    int show_udp = 0;
    int show_route = 0;
    int show_iface = 0;
    int show_stats = 0;
    int numeric = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            puts("netstat (akaOS Network Utilities) 3.0");
            return 0;
        }
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--all") == 0) {
            show_all = 1;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--listening") == 0) {
            show_listen = 1;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--tcp") == 0) {
            show_tcp = 1;
        } else if (strcmp(arg, "-u") == 0 || strcmp(arg, "--udp") == 0) {
            show_udp = 1;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--route") == 0) {
            show_route = 1;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--interfaces") == 0) {
            show_iface = 1;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--statistics") == 0) {
            show_stats = 1;
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--numeric") == 0) {
            numeric = 1;
        } else if (arg[0] == '-' && arg[1] != '-') {
            for (size_t j = 1; arg[j]; ++j) {
                switch (arg[j]) {
                    case 'a': show_all = 1; break;
                    case 'l': show_listen = 1; break;
                    case 't': show_tcp = 1; break;
                    case 'u': show_udp = 1; break;
                    case 'r': show_route = 1; break;
                    case 'i': show_iface = 1; break;
                    case 's': show_stats = 1; break;
                    case 'n': numeric = 1; break;
                    default:
                        fprintf(stderr, "netstat: invalid option -- '%c'\n", arg[j]);
                        return 1;
                }
            }
        }
    }

    if (show_stats) {
        show_statistics();
        return 0;
    }

    if (show_route) {
        show_routing_table(numeric);
        return 0;
    }

    if (show_iface) {
        show_interfaces();
        return 0;
    }

    if (!show_tcp && !show_udp) {
        show_tcp = 1;
        show_udp = 1;
    }

    show_sockets(show_tcp, show_udp, show_all, show_listen, numeric);
    return 0;
}
