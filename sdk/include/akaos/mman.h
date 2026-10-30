#ifndef AKAOS_MMAN_H
#define AKAOS_MMAN_H

#include <stdint.h>

#define AKAOS_PROT_NONE  0x0
#define AKAOS_PROT_READ  0x1
#define AKAOS_PROT_WRITE 0x2
#define AKAOS_PROT_EXEC  0x4

#define AKAOS_MAP_PRIVATE   0x02
#define AKAOS_MAP_SHARED    0x01
#define AKAOS_MAP_FIXED     0x10
#define AKAOS_MAP_ANONYMOUS 0x20

#define AKAOS_SHM_NAME_MAX 31
/* Size shared-memory objects to each architecture's bounded mmap capacity.
 * A 1024x768x32bpp Wayland buffer already needs 3 MiB. */
#if defined(__x86_64__)
#define AKAOS_SHM_MAX_SIZE (UINT64_C(16384) * UINT64_C(4096))
#elif defined(__riscv)
#define AKAOS_SHM_MAX_SIZE (UINT64_C(2048) * UINT64_C(4096))
#else
#define AKAOS_SHM_MAX_SIZE (UINT64_C(1024) * UINT64_C(4096))
#endif

struct akaos_shm_open_args {
    uint64_t name;
    uint64_t name_length;
    uint64_t flags;
    uint32_t mode;
    uint32_t reserved;
};

struct akaos_mmap_args {
    uint64_t address;
    uint64_t length;
    uint64_t protection;
    uint64_t flags;
    int64_t descriptor;
    uint64_t offset;
};

#endif
