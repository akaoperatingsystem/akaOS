#ifndef _LINUX_NVME_SHIM_H
#define _LINUX_NVME_SHIM_H

#include <stddef.h>
#include <stdint.h>

// Basic Linux types mapping
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define __init
#define __exit
#define module_init(x)
#define module_exit(x)
#define MODULE_LICENSE(x)

#endif
