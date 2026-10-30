#ifndef AKAOS_LIBC_SYS_SYSMACROS_H
#define AKAOS_LIBC_SYS_SYSMACROS_H

#include <stdint.h>
#include <sys/types.h>

#ifndef major
#define major(device) \
    ((unsigned int)((((uint64_t)(device) >> 8U) & UINT64_C(0xfff)) | \
                    ((uint64_t)(device) >> 32U)))
#endif

#ifndef minor
#define minor(device) \
    ((unsigned int)(((uint64_t)(device) & UINT64_C(0xff)) | \
                    (((uint64_t)(device) >> 12U) & UINT64_C(0xffffff00))))
#endif

#ifndef makedev
#define makedev(major_value, minor_value) \
    ((dev_t)((((uint64_t)(major_value) & UINT64_C(0xfffff000)) << 32U) | \
             (((uint64_t)(major_value) & UINT64_C(0xfff)) << 8U) | \
             (((uint64_t)(minor_value) & UINT64_C(0xffffff00)) << 12U) | \
             ((uint64_t)(minor_value) & UINT64_C(0xff))))
#endif

#endif
