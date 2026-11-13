/*
 * ethtool - Query or control network driver and interface statistics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <akaos/ioctl.h>
#include <akaos/net.h>

static void print_usage(void) {
    puts("Usage: ethtool [options] <interface>\n"
         "Query or control network driver and interface statistics.\n\n"
         "Options:\n"
         "  -S, --statistics  show adapter and interface statistics\n"
         "  -i, --driver      show driver information\n"
         "  -h, --help        display this help message and exit\n"
         "  -V, --version     display version information and exit\n");
}

int main(int argc, char **argv) {
    int show_stats = 0;
    int show_driver = 0;
    const char *ifname = "eth0";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("ethtool (akaOS netutils) 1.0");
            return 0;
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--statistics") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--driver") == 0) {
            show_driver = 1;
        } else if (argv[i][0] != '-') {
            ifname = argv[i];
        } else {
            fprintf(stderr, "ethtool: unrecognized option '%s'\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    struct akaos_net_stats stats;
    memset(&stats, 0, sizeof(stats));

    int fd = open("/dev/net", O_RDWR);
    if (fd >= 0) {
        (void)ioctl(fd, AKAOS_SIOCGIFSTATS, &stats);
        close(fd);
    }

    if (show_driver) {
        printf("driver: virtio_net\n");
        printf("version: 1.0\n");
        printf("firmware-version: N/A\n");
        printf("bus-info: 0000:00:03.0\n");
        printf("supports-statistics: yes\n");
        return 0;
    }

    if (show_stats) {
        printf("NIC statistics for %s:\n", ifname);
        printf("     rx_packets: %u\n", stats.rx_packets);
        printf("     tx_packets: %u\n", stats.tx_packets);
        printf("     rx_bytes: %u\n", stats.rx_bytes);
        printf("     tx_bytes: %u\n", stats.tx_bytes);
        printf("     rx_errors: 0\n");
        printf("     tx_errors: 0\n");
        printf("     rx_dropped: 0\n");
        printf("     tx_dropped: 0\n");
        return 0;
    }

    /* Default settings summary */
    printf("Settings for %s:\n", ifname);
    printf("    Supported ports: [ TP ]\n");
    printf("    Supported link modes:   10baseT/Half 10baseT/Full\n"
           "                            100baseT/Half 100baseT/Full\n"
           "                            1000baseT/Full\n");
    printf("    Speed: 1000Mb/s\n");
    printf("    Duplex: Full\n");
    printf("    Auto-negotiation: on\n");
    printf("    Port: Twisted Pair\n");
    printf("    Link detected: yes\n");
    return 0;
}
