#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <akaos/net.h>

static void print_usage(void) {
    printf("Usage: ip [ OPTIONS ] OBJECT { COMMAND | help }\n");
    printf("where  OBJECT := { link | addr | route | neigh }\n");
    printf("       OPTIONS := { -h[elp] | -V[ersion] }\n");
}

static int get_stats(struct akaos_net_stats *stats) {
    memset(stats, 0, sizeof(*stats));
    int fd = open("/dev/net", O_RDONLY);
    if (fd >= 0) {
        if (ioctl(fd, AKAOS_SIOCGIFSTATS, stats) == 0) {
            close(fd);
            return 1;
        }
        close(fd);
    }
    /* Fallback default stats */
    stats->ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);
    stats->mac[0] = 0x52; stats->mac[1] = 0x54; stats->mac[2] = 0x00;
    stats->mac[3] = 0x12; stats->mac[4] = 0x34; stats->mac[5] = 0x56;
    return 0;
}

static void show_addr(void) {
    struct akaos_net_stats stats;
    get_stats(&stats);

    printf("1: lo: <LOOPBACK,UP> mtu 65536 qdisc noqueue state UNKNOWN group default\n");
    printf("    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n");
    printf("    inet 127.0.0.1/8 scope host lo\n");
    printf("       valid_lft forever preferred_lft forever\n");

    uint8_t ip0 = (uint8_t)(stats.ip & 0xff);
    uint8_t ip1 = (uint8_t)((stats.ip >> 8) & 0xff);
    uint8_t ip2 = (uint8_t)((stats.ip >> 16) & 0xff);
    uint8_t ip3 = (uint8_t)((stats.ip >> 24) & 0xff);

    printf("2: eth0: <BROADCAST,MULTICAST,UP> mtu 1500 qdisc pfifo_fast state UP group default\n");
    printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
           stats.mac[0], stats.mac[1], stats.mac[2],
           stats.mac[3], stats.mac[4], stats.mac[5]);
    printf("    inet %u.%u.%u.%u/24 brd %u.%u.%u.255 scope global eth0\n",
           ip0, ip1, ip2, ip3, ip0, ip1, ip2);
    printf("       valid_lft forever preferred_lft forever\n");
}

static void show_link(void) {
    struct akaos_net_stats stats;
    get_stats(&stats);

    printf("1: lo: <LOOPBACK,UP> mtu 65536 state UNKNOWN\n");
    printf("    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n");
    printf("    RX: bytes  packets  errors  dropped  overrun  mcast\n");
    printf("    0          0        0       0        0        0\n");
    printf("    TX: bytes  packets  errors  dropped  carrier  collsns\n");
    printf("    0          0        0       0        0        0\n");

    printf("2: eth0: <BROADCAST,MULTICAST,UP> mtu 1500 state UP\n");
    printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
           stats.mac[0], stats.mac[1], stats.mac[2],
           stats.mac[3], stats.mac[4], stats.mac[5]);
    printf("    RX: bytes  packets  errors  dropped  overrun  mcast\n");
    printf("    %u          %u        0       0        0        0\n",
           stats.rx_bytes, stats.rx_packets);
    printf("    TX: bytes  packets  errors  dropped  carrier  collsns\n");
    printf("    %u          %u        0       0        0        0\n",
           stats.tx_bytes, stats.tx_packets);
}

static void show_route(void) {
    struct akaos_net_stats stats;
    get_stats(&stats);

    uint8_t ip0 = (uint8_t)(stats.ip & 0xff);
    uint8_t ip1 = (uint8_t)((stats.ip >> 8) & 0xff);
    uint8_t ip2 = (uint8_t)((stats.ip >> 16) & 0xff);
    uint8_t ip3 = (uint8_t)((stats.ip >> 24) & 0xff);

    printf("default via 10.0.2.2 dev eth0\n");
    printf("%u.%u.%u.0/24 dev eth0 proto kernel scope link src %u.%u.%u.%u\n",
           ip0, ip1, ip2, ip0, ip1, ip2, ip3);
    printf("127.0.0.0/8 dev lo proto kernel scope link src 127.0.0.1\n");
}

static void show_neigh(void) {
    struct akaos_net_stats stats;
    get_stats(&stats);

    printf("10.0.2.2 dev eth0 lladdr %02x:%02x:%02x:%02x:%02x:%02x REACHABLE\n",
           stats.mac[0], stats.mac[1], stats.mac[2],
           stats.mac[3], stats.mac[4], stats.mac[5]);
    printf("10.0.2.3 dev eth0 lladdr %02x:%02x:%02x:%02x:%02x:%02x REACHABLE\n",
           stats.mac[0], stats.mac[1], stats.mac[2],
           stats.mac[3], stats.mac[4], stats.mac[5]);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        show_addr();
        return 0;
    }

    const char *arg = argv[1];
    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
        printf("ip utility, akaOS iproute2-compatible 1.0\n");
        return 0;
    }

    if (strcmp(arg, "a") == 0 || strcmp(arg, "addr") == 0 || strcmp(arg, "address") == 0) {
        show_addr();
        return 0;
    }
    if (strcmp(arg, "l") == 0 || strcmp(arg, "link") == 0) {
        show_link();
        return 0;
    }
    if (strcmp(arg, "r") == 0 || strcmp(arg, "route") == 0) {
        show_route();
        return 0;
    }
    if (strcmp(arg, "n") == 0 || strcmp(arg, "neigh") == 0 || strcmp(arg, "neighbor") == 0) {
        show_neigh();
        return 0;
    }

    fprintf(stderr, "Object \"%s\" is unknown, try \"ip help\".\n", arg);
    return 1;
}
