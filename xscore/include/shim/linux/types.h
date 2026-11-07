#ifndef _SHIM_LINUX_TYPES_H
#define _SHIM_LINUX_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

typedef uint32_t gfp_t;
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

typedef uint32_t gfp_t;
<<<<<<< HEAD
=======
>>>>>>> 09b0a3cd2072608cac634b0d859c1e2c9f88cb69
>>>>>>> pr-6

#define GFP_KERNEL 0
#define GFP_ATOMIC 1

#endif /* _SHIM_LINUX_TYPES_H */
