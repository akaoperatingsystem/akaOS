#ifndef XSCORE_CPU_H
#define XSCORE_CPU_H

#include <stdint.h>
#include <stddef.h>

struct xscore_cpu_features {
    /* Legacy & Base SIMD */
    uint32_t fpu:1;
    uint32_t mmx:1;
    uint32_t sse:1;
    uint32_t sse2:1;
    uint32_t sse3:1;
    uint32_t ssse3:1;
    uint32_t sse4_1:1;
    uint32_t sse4_2:1;

    /* Advanced Vector Extensions */
    uint32_t avx:1;
    uint32_t avx2:1;
    uint32_t fma:1;
    uint32_t f16c:1;

    /* AVX-512 Suite */
    uint32_t avx512f:1;
    uint32_t avx512dq:1;
    uint32_t avx512cd:1;
    uint32_t avx512er:1;
    uint32_t avx512pf:1;
    uint32_t avx512bw:1;
    uint32_t avx512vl:1;
    uint32_t avx512_ifma:1;
    uint32_t avx512_vbmi:1;
    uint32_t avx512_vbmi2:1;
    uint32_t avx512_vnni:1;
    uint32_t avx512_bitalg:1;
    uint32_t avx512_vpopcntdq:1;
    uint32_t avx512_bf16:1;
    uint32_t avx512_fp16:1;

    /* Advanced Matrix Extensions (AMX) */
    uint32_t amx_tile:1;
    uint32_t amx_int8:1;
    uint32_t amx_bf16:1;
    uint32_t amx_fp16:1;

    /* Extended State Management */
    uint32_t xsave:1;
    uint32_t xsaveopt:1;
    uint32_t xsavec:1;
    uint32_t xsaves:1;

    /* Hardware Security & Protection */
    uint32_t nx:1;
    uint32_t smep:1;
    uint32_t smap:1;
    uint32_t umip:1;
    uint32_t pku:1;
    uint32_t cet_ss:1;
    uint32_t cet_ibt:1;

    /* Cryptography & Hardware Math */
    uint32_t aes:1;
    uint32_t sha_ni:1;
    uint32_t rdrand:1;
    uint32_t rdseed:1;
    uint32_t pclmulqdq:1;
    uint32_t popcnt:1;
    uint32_t movbe:1;
    uint32_t bmi1:1;
    uint32_t bmi2:1;
    uint32_t adx:1;
    uint32_t gfni:1;
    uint32_t vaes:1;
    uint32_t vpclmulqdq:1;

    /* System & Instruction Primitives */
    uint32_t fsgsbase:1;
    uint32_t tsc_deadline:1;
    uint32_t invariant_tsc:1;
    uint32_t clflush:1;
    uint32_t clflushopt:1;
    uint32_t clwb:1;

    /* ARM & AArch64 Capabilities */
    uint32_t neon:1;
    uint32_t sve:1;
    uint32_t sve2:1;
    uint32_t sme:1;
    uint32_t pac:1;
    uint32_t bti:1;
    uint32_t mte:1;

    /* RISC-V Capabilities */
    uint32_t rvv:1;
    uint32_t rvb:1;
    uint32_t rvc:1;
};

struct xscore_cpu_info {
    char vendor[16];
    char brand[64];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t type;
    uint32_t mhz;
    uint32_t bogomips_int;
    uint32_t bogomips_frac;

    /* Cache Hierarchy */
    uint32_t l1d_size_kb;
    uint32_t l1i_size_kb;
    uint32_t l2_size_kb;
    uint32_t l3_size_kb;
    uint32_t cache_size_kb;      /* Total / highest level cache */
    uint32_t cache_line_bytes;

    /* Topology */
    uint32_t sockets;
    uint32_t cores_per_socket;
    uint32_t threads_per_core;
    uint32_t total_logical_cpus;

    /* Addressing & Extended State */
    uint32_t phys_addr_bits;
    uint32_t virt_addr_bits;
    uint32_t xsave_size_bytes;
    char active_simd[32];        /* e.g. "AVX-512", "AVX2", "NEON", "SVE", "RVV" */

    /* Feature Flags (Linux /proc/cpuinfo string format) */
    char flags[1024];

    /* Structured Feature Bitmask */
    struct xscore_cpu_features features;
};

void xscore_cpu_initialize(void);
const struct xscore_cpu_info *xscore_cpu_get_info(void);
int xscore_cpu_has_feature(const char *name);
void xscore_cpu_enable_hardware_features(void);
size_t xscore_cpu_format_proc_cpuinfo(char *buf, size_t max_size);

#endif /* XSCORE_CPU_H */
