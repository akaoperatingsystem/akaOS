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

/* IP helper */
uint32_t net_make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void     net_format_ip(uint32_t ip, char *buf);

#endif
