/* ============================================================
 * akaOS — Network Stack Header (PCI + e1000 + ARP/IP/ICMP)
 * ============================================================ */
#ifndef NET_H
#define NET_H
#include <stdint.h>

/* Initialize networking (PCI scan, e1000 init) */
int  net_init(void);
int  net_is_available(void);

/* Ping a host. Returns round-trip time in ms, or -1 on failure. */
int  net_ping(uint32_t ip, int timeout_ms);

/* Poll network RX and handle ARP/IP/TCP for client connections. */
void net_poll(void);

/* Minimal HTTP/1.0 client (no TLS, no chunked encoding). Returns bytes written, or -1. */
int  net_http_get(uint32_t ip, int port, const char *host, const char *path,
                  char *out, int out_sz, int timeout_ms);

/* Basic counters (packets seen by the driver). */
void net_get_packet_counts(uint32_t *tx_packets, uint32_t *rx_packets);
void net_get_e1000_debug(uint32_t *status, uint32_t *rctl, uint32_t *rdh, uint32_t *rdt);

/* IP helper */
uint32_t net_make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void     net_format_ip(uint32_t ip, char *buf);

#endif
