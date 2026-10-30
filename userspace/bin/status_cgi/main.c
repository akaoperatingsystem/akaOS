#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <akaos/net.h>

int main(void) {
    printf("Content-Type: text/html\r\n\r\n");
    printf("<!DOCTYPE html><html><head><title>akaOS System Status</title></head><body>");
    printf("<h1>akaOS Dynamic CGI Status</h1>");
    printf("<p><b>Status:</b> Online</p>");

    int pfd = open("/proc/processes", O_RDONLY);
    if (pfd >= 0) {
        char buf[512];
        ssize_t n = read(pfd, buf, sizeof(buf) - 1);
        close(pfd);
        if (n > 0) {
            buf[n] = '\0';
            printf("<h3>Running Processes:</h3><pre>%s</pre>", buf);
        }
    }

    int nfd = open("/dev/net", O_RDONLY);
    if (nfd >= 0) {
        struct akaos_net_stats stats;
        if (ioctl(nfd, AKAOS_SIOCGIFSTATS, &stats) == 0) {
            printf("<h3>Network Interface (eth0):</h3>");
            printf("<p>MAC: %02x:%02x:%02x:%02x:%02x:%02x | IP: %u.%u.%u.%u<br>",
                   stats.mac[0], stats.mac[1], stats.mac[2],
                   stats.mac[3], stats.mac[4], stats.mac[5],
                   (unsigned)(stats.ip & 0xff), (unsigned)((stats.ip >> 8) & 0xff),
                   (unsigned)((stats.ip >> 16) & 0xff), (unsigned)((stats.ip >> 24) & 0xff));
            printf("RX Packets: %u (%u bytes) | TX Packets: %u (%u bytes)</p>",
                   stats.rx_packets, stats.rx_bytes, stats.tx_packets, stats.tx_bytes);
        }
        close(nfd);
    }

    printf("<hr><p><i>Served dynamically by akaOS microhttpd CGI engine</i></p>");
    printf("</body></html>\n");
    return 0;
}
