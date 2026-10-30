#ifndef AKAOS_LIBC_AKAOS_CRC32_H
#define AKAOS_LIBC_AKAOS_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t akaos_crc32(uint32_t crc, const void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif
