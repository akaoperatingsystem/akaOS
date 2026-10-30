#ifndef AKAOS_LIBC_ENDIAN_H
#define AKAOS_LIBC_ENDIAN_H

#include <byteswap.h>
#include <stdint.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __PDP_ENDIAN
#define __PDP_ENDIAN 3412
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define __BYTE_ORDER __LITTLE_ENDIAN
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define __BYTE_ORDER __BIG_ENDIAN
#else
#error "akaOS target byte order is not defined by the compiler"
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#endif
#ifndef PDP_ENDIAN
#define PDP_ENDIAN __PDP_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER __BYTE_ORDER
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe16(value) bswap_16(value)
#define htole16(value) ((uint16_t)(value))
#define be16toh(value) bswap_16(value)
#define le16toh(value) ((uint16_t)(value))
#define htobe32(value) bswap_32(value)
#define htole32(value) ((uint32_t)(value))
#define be32toh(value) bswap_32(value)
#define le32toh(value) ((uint32_t)(value))
#define htobe64(value) bswap_64(value)
#define htole64(value) ((uint64_t)(value))
#define be64toh(value) bswap_64(value)
#define le64toh(value) ((uint64_t)(value))
#else
#define htobe16(value) ((uint16_t)(value))
#define htole16(value) bswap_16(value)
#define be16toh(value) ((uint16_t)(value))
#define le16toh(value) bswap_16(value)
#define htobe32(value) ((uint32_t)(value))
#define htole32(value) bswap_32(value)
#define be32toh(value) ((uint32_t)(value))
#define le32toh(value) bswap_32(value)
#define htobe64(value) ((uint64_t)(value))
#define htole64(value) bswap_64(value)
#define be64toh(value) ((uint64_t)(value))
#define le64toh(value) bswap_64(value)
#endif

#endif
