#ifndef AKAOS_LIBC_BYTESWAP_H
#define AKAOS_LIBC_BYTESWAP_H

#include <stdint.h>

#ifndef bswap_16
#define bswap_16(value) __builtin_bswap16((uint16_t)(value))
#endif
#ifndef bswap_32
#define bswap_32(value) __builtin_bswap32((uint32_t)(value))
#endif
#ifndef bswap_64
#define bswap_64(value) __builtin_bswap64((uint64_t)(value))
#endif

#endif
