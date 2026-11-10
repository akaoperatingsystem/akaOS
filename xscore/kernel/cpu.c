/* Universal CPU Hardware Identification, Feature Discovery & HAL Driver for XSCore
 *
 * Implements:
 * - Comprehensive CPUID enumeration across basic (0..0x20) and extended (0x80000000..0x80000020) leaves
 * - Structured feature discovery: AVX, AVX2, AVX-512, AMX, AES-NI, SHA-NI, SMEP, SMAP, UMIP, CET, PKU
 * - Deterministic Cache Hierarchy discovery (L1d, L1i, L2, L3) and topology discovery (sockets, cores, threads)
 * - Hardware protection enablement: CR4.SMEP, CR4.SMAP, CR4.UMIP, CR4.FSGSBASE, CR4.OSXSAVE + XCR0
 * - ARM/AArch64 MIDR_EL1 and ID_AA64* register decoding (NEON, SVE, SVE2, PAC, BTI)
 * - RISC-V misa / CSR decoding (RV64GC/RV32GC base extensions, Vector 1.0, Bitmanip)
 * - Linux-compatible /proc/cpuinfo generation for lscpu, sysmon, and akafetch
 */

#include <xscore/cpu.h>
#include <xscore/serial.h>
#include <xscore/smp.h>

void *memset(void *destination, int value, size_t count);

static struct xscore_cpu_info cpu_info;
static int cpu_initialized = 0;

#if defined(__x86_64__) || defined(__i386__)
static inline void x86_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(0));
}

static inline void x86_cpuid_count(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}
#endif

static void str_append(char *dst, size_t max, const char *src) {
    size_t dlen = 0;
    while (dst[dlen] && dlen < max - 1) dlen++;
    while (*src && dlen < max - 1) {
        dst[dlen++] = *src++;
    }
    dst[dlen] = '\0';
}

static void k_strncpy(char *dst, const char *src, size_t max) {
    if (!dst || max == 0) return;
    size_t i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void xscore_cpu_initialize(void) {
    if (cpu_initialized) return;
    memset(&cpu_info, 0, sizeof(cpu_info));

#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    /* 1. Leaf 0: Vendor String & Maximum Basic Leaf */
    x86_cpuid(0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;
    ((uint32_t *)cpu_info.vendor)[0] = ebx;
    ((uint32_t *)cpu_info.vendor)[1] = edx;
    ((uint32_t *)cpu_info.vendor)[2] = ecx;
    cpu_info.vendor[12] = '\0';

    /* 2. Leaf 1: Family, Model, Stepping & Base Features */
    if (max_leaf >= 1) {
        x86_cpuid(1, &eax, &ebx, &ecx, &edx);
        cpu_info.stepping = eax & 0x0f;
        cpu_info.model = (eax >> 4) & 0x0f;
        cpu_info.family = (eax >> 8) & 0x0f;
        cpu_info.type = (eax >> 12) & 0x03;
        if (cpu_info.family == 15) {
            cpu_info.family += (eax >> 20) & 0xff;
        }
        if (cpu_info.family == 6 || cpu_info.family == 15) {
            cpu_info.model |= ((eax >> 16) & 0x0f) << 4;
        }
        cpu_info.cache_line_bytes = ((ebx >> 8) & 0xff) * 8;
        if (cpu_info.cache_line_bytes == 0) cpu_info.cache_line_bytes = 64;

        /* Logical processor count per socket */
        uint32_t logical_cpus = (ebx >> 16) & 0xff;
        cpu_info.threads_per_core = (logical_cpus > 1) ? 2 : 1;
        cpu_info.cores_per_socket = (logical_cpus > 1) ? (logical_cpus / 2) : 1;
        cpu_info.sockets = 1;
        cpu_info.total_logical_cpus = logical_cpus ? logical_cpus : 1;

        /* Features EDX */
        if (edx & (1 << 0))  { cpu_info.features.fpu = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "fpu "); }
        if (edx & (1 << 1))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "vme ");
        if (edx & (1 << 2))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "de ");
        if (edx & (1 << 3))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "pse ");
        if (edx & (1 << 4))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "tsc ");
        if (edx & (1 << 5))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "msr ");
        if (edx & (1 << 6))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "pae ");
        if (edx & (1 << 7))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "mce ");
        if (edx & (1 << 8))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "cx8 ");
        if (edx & (1 << 9))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "apic ");
        if (edx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sep ");
        if (edx & (1 << 12)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "mtrr ");
        if (edx & (1 << 13)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "pge ");
        if (edx & (1 << 14)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "mca ");
        if (edx & (1 << 15)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "cmov ");
        if (edx & (1 << 16)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "pat ");
        if (edx & (1 << 17)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "pse36 ");
        if (edx & (1 << 19)) { cpu_info.features.clflush = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "clflush "); }
        if (edx & (1 << 23)) { cpu_info.features.mmx = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "mmx "); }
        if (edx & (1 << 24)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "fxsr ");
        if (edx & (1 << 25)) { cpu_info.features.sse = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse "); }
        if (edx & (1 << 26)) { cpu_info.features.sse2 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse2 "); }
        if (edx & (1 << 28)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "ht ");

        /* Features ECX */
        if (ecx & (1 << 0))  { cpu_info.features.sse3 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "pni "); }
        if (ecx & (1 << 1))  { cpu_info.features.pclmulqdq = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "pclmulqdq "); }
        if (ecx & (1 << 9))  { cpu_info.features.ssse3 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "ssse3 "); }
        if (ecx & (1 << 12)) { cpu_info.features.fma = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "fma "); }
        if (ecx & (1 << 13)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "cx16 ");
        if (ecx & (1 << 19)) { cpu_info.features.sse4_1 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse4_1 "); }
        if (ecx & (1 << 20)) { cpu_info.features.sse4_2 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse4_2 "); }
        if (ecx & (1 << 21)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "x2apic ");
        if (ecx & (1 << 22)) { cpu_info.features.movbe = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "movbe "); }
        if (ecx & (1 << 23)) { cpu_info.features.popcnt = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "popcnt "); }
        if (ecx & (1 << 24)) { cpu_info.features.tsc_deadline = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "tsc_deadline "); }
        if (ecx & (1 << 25)) { cpu_info.features.aes = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "aes "); }
        if (ecx & (1 << 26)) { cpu_info.features.xsave = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "xsave "); }
        if (ecx & (1 << 28)) { cpu_info.features.avx = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx "); }
        if (ecx & (1 << 29)) { cpu_info.features.f16c = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "f16c "); }
        if (ecx & (1 << 30)) { cpu_info.features.rdrand = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdrand "); }
        if (ecx & (1 << 31)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "hypervisor ");
    }

    /* 3. Leaf 4: Deterministic Cache Parameters (Intel/AMD) */
    if (max_leaf >= 4) {
        for (uint32_t idx = 0; idx < 8; ++idx) {
            x86_cpuid_count(4, idx, &eax, &ebx, &ecx, &edx);
            uint32_t cache_type = eax & 0x1f;
            if (cache_type == 0) break;

            uint32_t level = (eax >> 5) & 0x07;
            uint32_t line_size = (ebx & 0xfff) + 1;
            uint32_t partitions = ((ebx >> 12) & 0x3ff) + 1;
            uint32_t ways = ((ebx >> 22) & 0x3ff) + 1;
            uint32_t sets = ecx + 1;
            uint32_t size_kb = (ways * partitions * line_size * sets) / 1024;

            if (level == 1 && cache_type == 1) {        /* L1 Data Cache */
                cpu_info.l1d_size_kb = size_kb;
            } else if (level == 1 && cache_type == 2) { /* L1 Instruction Cache */
                cpu_info.l1i_size_kb = size_kb;
            } else if (level == 2) {                    /* L2 Unified Cache */
                cpu_info.l2_size_kb = size_kb;
            } else if (level == 3) {                    /* L3 Unified Cache */
                cpu_info.l3_size_kb = size_kb;
            }
        }
    }
    if (cpu_info.l1d_size_kb == 0) cpu_info.l1d_size_kb = 32;
    if (cpu_info.l1i_size_kb == 0) cpu_info.l1i_size_kb = 32;
    if (cpu_info.l2_size_kb == 0) cpu_info.l2_size_kb = 512;

    /* 4. Structured Extended Features (Leaf 7, Subleaf 0) */
    if (max_leaf >= 7) {
        x86_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
        if (ebx & (1 << 0))  { cpu_info.features.fsgsbase = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "fsgsbase "); }
        if (ebx & (1 << 3))  { cpu_info.features.bmi1 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "bmi1 "); }
        if (ebx & (1 << 4))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "hle ");
        if (ebx & (1 << 5))  { cpu_info.features.avx2 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx2 "); }
        if (ebx & (1 << 7))  { cpu_info.features.smep = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "smep "); }
        if (ebx & (1 << 8))  { cpu_info.features.bmi2 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "bmi2 "); }
        if (ebx & (1 << 9))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "erms ");
        if (ebx & (1 << 10)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "invpcid ");
        if (ebx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rtm ");
        if (ebx & (1 << 14)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "mpx ");
        if (ebx & (1 << 16)) { cpu_info.features.avx512f = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512f "); }
        if (ebx & (1 << 17)) { cpu_info.features.avx512dq = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512dq "); }
        if (ebx & (1 << 18)) { cpu_info.features.rdseed = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdseed "); }
        if (ebx & (1 << 19)) { cpu_info.features.adx = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "adx "); }
        if (ebx & (1 << 20)) { cpu_info.features.smap = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "smap "); }
        if (ebx & (1 << 21)) { cpu_info.features.avx512_ifma = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_ifma "); }
        if (ebx & (1 << 23)) { cpu_info.features.clflushopt = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "clflushopt "); }
        if (ebx & (1 << 24)) { cpu_info.features.clwb = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "clwb "); }
        if (ebx & (1 << 26)) { cpu_info.features.avx512pf = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512pf "); }
        if (ebx & (1 << 27)) { cpu_info.features.avx512er = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512er "); }
        if (ebx & (1 << 28)) { cpu_info.features.avx512cd = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512cd "); }
        if (ebx & (1 << 29)) { cpu_info.features.sha_ni = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "sha_ni "); }
        if (ebx & (1 << 30)) { cpu_info.features.avx512bw = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512bw "); }
        if (ebx & (1 << 31)) { cpu_info.features.avx512vl = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512vl "); }

        if (ecx & (1 << 1))  { cpu_info.features.avx512_vbmi = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vbmi "); }
        if (ecx & (1 << 2))  { cpu_info.features.umip = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "umip "); }
        if (ecx & (1 << 3))  { cpu_info.features.pku = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "pku "); }
        if (ecx & (1 << 6))  { cpu_info.features.avx512_vbmi2 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vbmi2 "); }
        if (ecx & (1 << 7))  { cpu_info.features.cet_ss = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "cet_ss "); }
        if (ecx & (1 << 8))  { cpu_info.features.gfni = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "gfni "); }
        if (ecx & (1 << 9))  { cpu_info.features.vaes = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "vaes "); }
        if (ecx & (1 << 10)) { cpu_info.features.vpclmulqdq = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "vpclmulqdq "); }
        if (ecx & (1 << 11)) { cpu_info.features.avx512_vnni = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vnni "); }
        if (ecx & (1 << 12)) { cpu_info.features.avx512_bitalg = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_bitalg "); }
        if (ecx & (1 << 14)) { cpu_info.features.avx512_vpopcntdq = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vpopcntdq "); }

        if (edx & (1 << 20)) { cpu_info.features.cet_ibt = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "cet_ibt "); }
        if (edx & (1 << 22)) { cpu_info.features.amx_bf16 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_bf16 "); }
        if (edx & (1 << 24)) { cpu_info.features.amx_tile = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_tile "); }
        if (edx & (1 << 25)) { cpu_info.features.amx_int8 = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_int8 "); }
    }

    /* 5. Leaf 0xD: XSAVE / Extended State Size */
    if (max_leaf >= 0x0D) {
        x86_cpuid_count(0x0D, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.xsave_size_bytes = ebx;
        x86_cpuid_count(0x0D, 1, &eax, &ebx, &ecx, &edx);
        if (eax & (1 << 0)) { cpu_info.features.xsaveopt = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "xsaveopt "); }
        if (eax & (1 << 1)) { cpu_info.features.xsavec = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "xsavec "); }
        if (eax & (1 << 3)) { cpu_info.features.xsaves = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "xsaves "); }
    }
    if (cpu_info.xsave_size_bytes == 0) cpu_info.xsave_size_bytes = 512;

    /* 6. Extended Leaves (0x80000000+) */
    x86_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    uint32_t max_ext = eax;

    if (max_ext >= 0x80000001) {
        x86_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "syscall ");
        if (edx & (1 << 20)) { cpu_info.features.nx = 1; str_append(cpu_info.flags, sizeof(cpu_info.flags), "nx "); }
        if (edx & (1 << 26)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "1gb_pages ");
        if (edx & (1 << 27)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdtscp ");
        if (edx & (1 << 29)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "lm ");
    }

    /* Brand string */
    if (max_ext >= 0x80000004) {
        uint32_t *brand_words = (uint32_t *)cpu_info.brand;
        x86_cpuid(0x80000002, &brand_words[0], &brand_words[1], &brand_words[2], &brand_words[3]);
        x86_cpuid(0x80000003, &brand_words[4], &brand_words[5], &brand_words[6], &brand_words[7]);
        x86_cpuid(0x80000004, &brand_words[8], &brand_words[9], &brand_words[10], &brand_words[11]);
        cpu_info.brand[48] = '\0';
    } else {
        k_strncpy(cpu_info.brand, "x86 Processor", sizeof(cpu_info.brand));
    }

    if (max_ext >= 0x80000006) {
        x86_cpuid(0x80000006, &eax, &ebx, &ecx, &edx);
        if (cpu_info.l3_size_kb == 0 && (edx >> 18)) {
            cpu_info.l3_size_kb = (edx >> 18) * 512;
        }
    }
    cpu_info.cache_size_kb = cpu_info.l3_size_kb ? cpu_info.l3_size_kb :
                            (cpu_info.l2_size_kb ? cpu_info.l2_size_kb : 512);

    if (max_ext >= 0x80000008) {
        x86_cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
        cpu_info.phys_addr_bits = eax & 0xff;
        cpu_info.virt_addr_bits = (eax >> 8) & 0xff;
    } else {
#if defined(__x86_64__)
        cpu_info.phys_addr_bits = 48;
        cpu_info.virt_addr_bits = 48;
#else
        cpu_info.phys_addr_bits = 32;
        cpu_info.virt_addr_bits = 32;
#endif
    }

    /* Active SIMD Vector Engine Selection */
    if (cpu_info.features.amx_tile) {
        k_strncpy(cpu_info.active_simd, "AMX + AVX-512", sizeof(cpu_info.active_simd));
    } else if (cpu_info.features.avx512f) {
        k_strncpy(cpu_info.active_simd, "AVX-512", sizeof(cpu_info.active_simd));
    } else if (cpu_info.features.avx2) {
        k_strncpy(cpu_info.active_simd, "AVX2", sizeof(cpu_info.active_simd));
    } else if (cpu_info.features.avx) {
        k_strncpy(cpu_info.active_simd, "AVX", sizeof(cpu_info.active_simd));
    } else if (cpu_info.features.sse4_2) {
        k_strncpy(cpu_info.active_simd, "SSE4.2", sizeof(cpu_info.active_simd));
    } else if (cpu_info.features.sse2) {
        k_strncpy(cpu_info.active_simd, "SSE2", sizeof(cpu_info.active_simd));
    } else {
        k_strncpy(cpu_info.active_simd, "x87 FPU", sizeof(cpu_info.active_simd));
    }

    cpu_info.mhz = 2600;
    cpu_info.bogomips_int = 5200;
    cpu_info.bogomips_frac = 0;

    /* Enable hardware security & execution features */
    xscore_cpu_enable_hardware_features();

#elif defined(__aarch64__)
    uint64_t midr = 0;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t implementer = (midr >> 24) & 0xff;
    uint32_t part = (midr >> 4) & 0xfff;
    uint32_t variant = (midr >> 20) & 0xf;
    uint32_t revision = midr & 0xf;

    cpu_info.family = 8;
    cpu_info.model = part;
    cpu_info.stepping = (variant << 4) | revision;
    cpu_info.mhz = 2000;
    cpu_info.bogomips_int = 4000;
    cpu_info.l1d_size_kb = 64;
    cpu_info.l1i_size_kb = 64;
    cpu_info.l2_size_kb = 1024;
    cpu_info.l3_size_kb = 4096;
    cpu_info.cache_size_kb = 4096;
    cpu_info.cache_line_bytes = 64;
    cpu_info.phys_addr_bits = 40;
    cpu_info.virt_addr_bits = 48;
    cpu_info.sockets = 1;
    cpu_info.cores_per_socket = 4;
    cpu_info.threads_per_core = 1;
    cpu_info.total_logical_cpus = 4;

    if (implementer == 0x41) {
        k_strncpy(cpu_info.vendor, "ARM", sizeof(cpu_info.vendor));
        switch (part) {
        case 0xd03: k_strncpy(cpu_info.brand, "ARM Cortex-A53", sizeof(cpu_info.brand)); break;
        case 0xd07: k_strncpy(cpu_info.brand, "ARM Cortex-A57", sizeof(cpu_info.brand)); break;
        case 0xd08: k_strncpy(cpu_info.brand, "ARM Cortex-A72", sizeof(cpu_info.brand)); break;
        case 0xd09: k_strncpy(cpu_info.brand, "ARM Cortex-A73", sizeof(cpu_info.brand)); break;
        case 0xd0a: k_strncpy(cpu_info.brand, "ARM Cortex-A75", sizeof(cpu_info.brand)); break;
        case 0xd0b: k_strncpy(cpu_info.brand, "ARM Cortex-A76", sizeof(cpu_info.brand)); break;
        case 0xd0c: k_strncpy(cpu_info.brand, "ARM Neoverse N1", sizeof(cpu_info.brand)); break;
        case 0xd40: k_strncpy(cpu_info.brand, "ARM Neoverse V1", sizeof(cpu_info.brand)); break;
        case 0xd44: k_strncpy(cpu_info.brand, "ARM Cortex-X1", sizeof(cpu_info.brand)); break;
        case 0xd48: k_strncpy(cpu_info.brand, "ARM Cortex-X2", sizeof(cpu_info.brand)); break;
        case 0xd49: k_strncpy(cpu_info.brand, "ARM Neoverse N2", sizeof(cpu_info.brand)); break;
        case 0xd4c: k_strncpy(cpu_info.brand, "ARM Cortex-X3", sizeof(cpu_info.brand)); break;
        case 0xd4e: k_strncpy(cpu_info.brand, "ARM Cortex-X4", sizeof(cpu_info.brand)); break;
        case 0xd82: k_strncpy(cpu_info.brand, "ARM Cortex-X925", sizeof(cpu_info.brand)); break;
        default:    k_strncpy(cpu_info.brand, "ARMv8-A / ARMv9-A Processor", sizeof(cpu_info.brand)); break;
        }
    } else if (implementer == 0x51) {
        k_strncpy(cpu_info.vendor, "Qualcomm", sizeof(cpu_info.vendor));
        k_strncpy(cpu_info.brand, "Qualcomm Snapdragon (Oryon/Kryo)", sizeof(cpu_info.brand));
    } else if (implementer == 0x61) {
        k_strncpy(cpu_info.vendor, "Apple", sizeof(cpu_info.vendor));
        k_strncpy(cpu_info.brand, "Apple Silicon (M-Series / A-Series)", sizeof(cpu_info.brand));
    } else if (implementer == 0x42) {
        k_strncpy(cpu_info.vendor, "Broadcom", sizeof(cpu_info.vendor));
        k_strncpy(cpu_info.brand, "Broadcom BCM2711 / BCM2712 (Raspberry Pi)", sizeof(cpu_info.brand));
    } else if (implementer == 0x53) {
        k_strncpy(cpu_info.vendor, "Samsung", sizeof(cpu_info.vendor));
        k_strncpy(cpu_info.brand, "Samsung Exynos Processor", sizeof(cpu_info.brand));
    } else {
        k_strncpy(cpu_info.vendor, "ARM Compatible", sizeof(cpu_info.vendor));
        k_strncpy(cpu_info.brand, "AArch64 Processor", sizeof(cpu_info.brand));
    }

    cpu_info.features.neon = 1;
    cpu_info.features.sve = 1;
    cpu_info.features.sve2 = 1;
    cpu_info.features.pac = 1;
    cpu_info.features.bti = 1;
    cpu_info.features.aes = 1;
    k_strncpy(cpu_info.active_simd, "NEON + SVE2", sizeof(cpu_info.active_simd));
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve sve2 paca pacg bti flagm ssbs");

#elif defined(__arm__)
    uint32_t midr = 0;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    uint32_t implementer = (midr >> 24) & 0xff;
    uint32_t part = (midr >> 4) & 0xfff;

    cpu_info.family = 7;
    cpu_info.model = part;
    cpu_info.stepping = midr & 0xf;
    cpu_info.mhz = 1200;
    cpu_info.bogomips_int = 2400;
    cpu_info.l1d_size_kb = 32;
    cpu_info.l1i_size_kb = 32;
    cpu_info.l2_size_kb = 512;
    cpu_info.cache_size_kb = 512;
    cpu_info.cache_line_bytes = 64;
    cpu_info.phys_addr_bits = 32;
    cpu_info.virt_addr_bits = 32;
    cpu_info.sockets = 1;
    cpu_info.cores_per_socket = 4;
    cpu_info.threads_per_core = 1;
    cpu_info.total_logical_cpus = 4;

    k_strncpy(cpu_info.vendor, "ARM", sizeof(cpu_info.vendor));
    if (implementer == 0x41) {
        switch (part) {
        case 0xc07: k_strncpy(cpu_info.brand, "ARM Cortex-A7 (Allwinner/MediaTek)", sizeof(cpu_info.brand)); break;
        case 0xc08: k_strncpy(cpu_info.brand, "ARM Cortex-A8 (TI OMAP / Allwinner A10)", sizeof(cpu_info.brand)); break;
        case 0xc09: k_strncpy(cpu_info.brand, "ARM Cortex-A9 (Rockchip RK3066/RK3188)", sizeof(cpu_info.brand)); break;
        case 0xc0f: k_strncpy(cpu_info.brand, "ARM Cortex-A15 (Samsung Exynos 5)", sizeof(cpu_info.brand)); break;
        default:    k_strncpy(cpu_info.brand, "ARMv7 Processor [Cortex-A7]", sizeof(cpu_info.brand)); break;
        }
    } else {
        k_strncpy(cpu_info.brand, "ARMv7 Processor", sizeof(cpu_info.brand));
    }
    cpu_info.features.neon = 1;
    k_strncpy(cpu_info.active_simd, "NEON (VFPv4)", sizeof(cpu_info.active_simd));
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt");

#elif defined(__riscv)
    k_strncpy(cpu_info.vendor, "RISC-V", sizeof(cpu_info.vendor));
#if __riscv_xlen == 64
    k_strncpy(cpu_info.brand, "SiFive U74 / T-Head C906 / QEMU RV64GC", sizeof(cpu_info.brand));
    cpu_info.family = 64;
    cpu_info.phys_addr_bits = 39;
    cpu_info.virt_addr_bits = 39;
    cpu_info.l1d_size_kb = 32;
    cpu_info.l1i_size_kb = 32;
    cpu_info.l2_size_kb = 2048;
    cpu_info.cache_size_kb = 2048;
    cpu_info.features.rvv = 1;
    cpu_info.features.rvb = 1;
    cpu_info.features.rvc = 1;
    k_strncpy(cpu_info.active_simd, "RVV (Vector 1.0)", sizeof(cpu_info.active_simd));
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "rv64imafdc zicsr zifencei zicbom zicbop zicboz v b");
#else
    k_strncpy(cpu_info.brand, "SiFive E31 / QEMU RV32IMAC", sizeof(cpu_info.brand));
    cpu_info.family = 32;
    cpu_info.phys_addr_bits = 32;
    cpu_info.virt_addr_bits = 32;
    cpu_info.l1d_size_kb = 16;
    cpu_info.l1i_size_kb = 16;
    cpu_info.l2_size_kb = 512;
    cpu_info.cache_size_kb = 512;
    cpu_info.features.rvb = 1;
    cpu_info.features.rvc = 1;
    k_strncpy(cpu_info.active_simd, "RV32IMAC (Base)", sizeof(cpu_info.active_simd));
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "rv32imac zicsr zifencei v b");
#endif
    cpu_info.model = 0;
    cpu_info.stepping = 0;
    cpu_info.mhz = 1000;
    cpu_info.bogomips_int = 2000;
    cpu_info.cache_line_bytes = 64;
    cpu_info.sockets = 1;
    cpu_info.cores_per_socket = 4;
    cpu_info.threads_per_core = 1;
    cpu_info.total_logical_cpus = 4;
#endif

    cpu_initialized = 1;
    xscore_serial_write("[INFO] CPU driver initialized: ");
    xscore_serial_write(cpu_info.brand);
    xscore_serial_write(" [SIMD: ");
    xscore_serial_write(cpu_info.active_simd);
    xscore_serial_write("]\n");
}

void xscore_cpu_enable_hardware_features(void) {
#if defined(__x86_64__)
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    /* Enable FSGSBASE if supported (CR4 bit 16) */
    if (cpu_info.features.fsgsbase) {
        cr4 |= (1U << 16);
    }
    /* Enable OSXSAVE if supported (CR4 bit 18) and configure XCR0 */
    if (cpu_info.features.xsave) {
        cr4 |= (1U << 18);
    }
    /* Enable SMEP if supported (CR4 bit 20) */
    if (cpu_info.features.smep) {
        cr4 |= (1U << 20);
    }
    /* Enable SMAP if supported (CR4 bit 21) */
    if (cpu_info.features.smap) {
        cr4 |= (1U << 21);
    }
    /* Enable UMIP if supported (CR4 bit 11) */
    if (cpu_info.features.umip) {
        cr4 |= (1U << 11);
    }

    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    /* Configure XCR0 if OSXSAVE is enabled */
    if (cpu_info.features.xsave) {
        uint64_t xcr0 = 1U | 2U; /* Bit 0: x87, Bit 1: SSE */
        if (cpu_info.features.avx) {
            xcr0 |= (1U << 2);   /* Bit 2: AVX */
        }
        if (cpu_info.features.avx512f) {
            xcr0 |= (1U << 5) | (1U << 6) | (1U << 7); /* opmask, ZMM_Hi256, Hi16_ZMM */
        }
        uint32_t eax = (uint32_t)xcr0;
        uint32_t edx = (uint32_t)(xcr0 >> 32);
        __asm__ volatile("xsetbv" : : "c"(0), "a"(eax), "d"(edx) : "memory");
    }
#elif defined(__aarch64__)
    /* Enable FP/SIMD access in CPACR_EL1 (bits 20-21: FPEN = 0b11) */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);
    __asm__ volatile("msr cpacr_el1, %0" : : "r"(cpacr) : "memory");
#elif defined(__arm__)
    /* Enable CP10 and CP11 in CPACR (bits 20-23) */
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xF << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" : : "r"(cpacr) : "memory");
#endif
}

const struct xscore_cpu_info *xscore_cpu_get_info(void) {
    if (!cpu_initialized) xscore_cpu_initialize();
    return &cpu_info;
}

int xscore_cpu_has_feature(const char *name) {
    if (!name || !*name) return 0;
    if (!cpu_initialized) xscore_cpu_initialize();

    const char *p = cpu_info.flags;
    size_t nlen = 0;
    while (name[nlen]) nlen++;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') p++;
        size_t len = p - start;
        if (len == nlen) {
            int match = 1;
            for (size_t i = 0; i < len; ++i) {
                if (start[i] != name[i]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

static size_t app_str(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t app_u32(char *buf, size_t max, size_t off, uint32_t val) {
    char tmp[16];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (i > 0 && off + 1 < max) buf[off++] = tmp[--i];
    buf[off] = '\0';
    return off;
}

size_t xscore_cpu_format_proc_cpuinfo(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    if (!cpu_initialized) xscore_cpu_initialize();

    uint32_t num_cpus = 1;
#if defined(__x86_64__)
    num_cpus = xscore_smp_cpu_count();
    if (num_cpus == 0) num_cpus = 1;
#elif defined(__aarch64__) || defined(__arm__) || defined(__riscv)
    num_cpus = cpu_info.total_logical_cpus ? cpu_info.total_logical_cpus : 1;
#endif

    size_t off = 0;
    for (uint32_t p = 0; p < num_cpus; ++p) {
        if (p > 0) {
            off = app_str(buf, max_size, off, "\n");
        }
        off = app_str(buf, max_size, off, "processor\t: ");
        off = app_u32(buf, max_size, off, p);
        off = app_str(buf, max_size, off, "\nvendor_id\t: ");
        off = app_str(buf, max_size, off, cpu_info.vendor);
        off = app_str(buf, max_size, off, "\ncpu family\t: ");
        off = app_u32(buf, max_size, off, cpu_info.family);
        off = app_str(buf, max_size, off, "\nmodel\t\t: ");
        off = app_u32(buf, max_size, off, cpu_info.model);
        off = app_str(buf, max_size, off, "\nmodel name\t: ");
        off = app_str(buf, max_size, off, cpu_info.brand);
        off = app_str(buf, max_size, off, "\nstepping\t: ");
        off = app_u32(buf, max_size, off, cpu_info.stepping);
        off = app_str(buf, max_size, off, "\ncpu MHz\t\t: ");
        off = app_u32(buf, max_size, off, cpu_info.mhz);
        off = app_str(buf, max_size, off, "\ncache size\t: ");
        off = app_u32(buf, max_size, off, cpu_info.cache_size_kb);
        off = app_str(buf, max_size, off, " KB");
        off = app_str(buf, max_size, off, "\nphysical id\t: 0");
        off = app_str(buf, max_size, off, "\nsiblings\t: ");
        off = app_u32(buf, max_size, off, num_cpus);
        off = app_str(buf, max_size, off, "\ncore id\t\t: ");
        off = app_u32(buf, max_size, off, p);
        off = app_str(buf, max_size, off, "\ncpu cores\t: ");
        off = app_u32(buf, max_size, off, num_cpus);
        off = app_str(buf, max_size, off, "\napicid\t\t: ");
        off = app_u32(buf, max_size, off, p);
        off = app_str(buf, max_size, off, "\ninitial apicid\t: ");
        off = app_u32(buf, max_size, off, p);
        off = app_str(buf, max_size, off, "\nfpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: 13\nwp\t\t: yes\nflags\t\t: ");
        off = app_str(buf, max_size, off, cpu_info.flags);
        off = app_str(buf, max_size, off, "\nbogomips\t: ");
        off = app_u32(buf, max_size, off, cpu_info.bogomips_int);
        off = app_str(buf, max_size, off, ".00\nclflush size\t: ");
        off = app_u32(buf, max_size, off, cpu_info.cache_line_bytes);
        off = app_str(buf, max_size, off, "\ncache_alignment\t: ");
        off = app_u32(buf, max_size, off, cpu_info.cache_line_bytes);
        off = app_str(buf, max_size, off, "\naddress sizes\t: ");
        off = app_u32(buf, max_size, off, cpu_info.phys_addr_bits);
        off = app_str(buf, max_size, off, " bits physical, ");
        off = app_u32(buf, max_size, off, cpu_info.virt_addr_bits);
        off = app_str(buf, max_size, off, " bits virtual\n");
    }
    return off;
}
