#ifndef AKAOS_LIBC_ARPA_NAMESER_H
#define AKAOS_LIBC_ARPA_NAMESER_H

#include <stdint.h>

/* Minimal Internet DNS class/type constants used by the POSIX resolver API.
 * Packet encoding/decoding is byte-oriented and does not expose host bitfields. */
typedef unsigned char u_char;

/* Wire-compatible 12-byte DNS message header. Multi-byte fields remain in
 * network order, matching the resolver API's GETSHORT/GETLONG helpers. */
typedef struct {
    uint16_t id;
    uint8_t flags1;
    uint8_t flags2;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} HEADER;

#define NS_INT16SZ 2
#define NS_INT32SZ 4
#define NS_GET16(value, cursor) do { \
    const u_char *_akaos_dns_p = (const u_char *)(cursor); \
    (value) = (uint16_t)(((uint16_t)_akaos_dns_p[0] << 8) | \
                         (uint16_t)_akaos_dns_p[1]); \
    (cursor) += NS_INT16SZ; \
} while (0)
#define NS_GET32(value, cursor) do { \
    const u_char *_akaos_dns_p = (const u_char *)(cursor); \
    (value) = ((uint32_t)_akaos_dns_p[0] << 24) | \
             ((uint32_t)_akaos_dns_p[1] << 16) | \
             ((uint32_t)_akaos_dns_p[2] << 8) | \
             (uint32_t)_akaos_dns_p[3]; \
    (cursor) += NS_INT32SZ; \
} while (0)
#define GETSHORT(value, cursor) NS_GET16(value, cursor)
#define GETLONG(value, cursor) NS_GET32(value, cursor)

#define MAXDNAME 1025

#ifdef __cplusplus
extern "C" {
#endif

int dn_expand(const u_char *message, const u_char *message_end,
              const u_char *compressed_name, char *expanded_name,
              int expanded_capacity);
int dn_skipname(const u_char *compressed_name, const u_char *message_end);

#ifdef __cplusplus
}
#endif

#define C_IN       1
#define C_INTERNET C_IN
#define C_CHAOS    3
#define C_HS       4
#define C_ANY      255

#define T_A      1
#define T_NS     2
#define T_CNAME  5
#define T_SOA    6
#define T_PTR    12
#define T_MX     15
#define T_TXT    16
#define T_AAAA   28
#define T_SRV    33
#define T_ANY    255

#endif
