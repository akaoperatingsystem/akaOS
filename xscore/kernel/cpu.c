/* Universal CPU Hardware Identification and HAL Driver for XSCore */
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
#endif

static void str_append(char *dst, size_t max, const char *src) {
    size_t dlen = 0;
    while (dst[dlen] && dlen < max - 1) dlen++;
    while (*src && dlen < max - 1) {
        dst[dlen++] = *src++;
    }
    dst[dlen] = '\0';
}

void xscore_cpu_initialize(void) {
    if (cpu_initialized) return;
    memset(&cpu_info, 0, sizeof(cpu_info));

#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    /* 1. Vendor string */
    x86_cpuid(0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;
    ((uint32_t *)cpu_info.vendor)[0] = ebx;
    ((uint32_t *)cpu_info.vendor)[1] = edx;
    ((uint32_t *)cpu_info.vendor)[2] = ecx;
    cpu_info.vendor[12] = '\0';

    /* 2. Family, Model, Stepping */
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

        /* Features EDX */
        if (edx & (1 << 0))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "fpu ");
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
        if (edx & (1 << 19)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "clflush ");
        if (edx & (1 << 23)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "mmx ");
        if (edx & (1 << 24)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "fxsr ");
        if (edx & (1 << 25)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse ");
        if (edx & (1 << 26)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse2 ");
        if (edx & (1 << 28)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "ht ");

        /* Features ECX */
        if (ecx & (1 << 0))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "pni ");
        if (ecx & (1 << 1))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "pclmulqdq ");
        if (ecx & (1 << 9))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "ssse3 ");
        if (ecx & (1 << 12)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "fma ");
        if (ecx & (1 << 13)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "cx16 ");
        if (ecx & (1 << 19)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse4_1 ");
        if (ecx & (1 << 20)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sse4_2 ");
        if (ecx & (1 << 21)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "x2apic ");
        if (ecx & (1 << 22)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "movbe ");
        if (ecx & (1 << 23)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "popcnt ");
        if (ecx & (1 << 24)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "tsc_deadline ");
        if (ecx & (1 << 25)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "aes ");
        if (ecx & (1 << 26)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "xsave ");
        if (ecx & (1 << 28)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx ");
        if (ecx & (1 << 29)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "f16c ");
        if (ecx & (1 << 30)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdrand ");
        if (ecx & (1 << 31)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "hypervisor ");
    }

    /* 3. Structured Extended Features (Leaf 7) */
    if (max_leaf >= 7) {
        x86_cpuid(7, &eax, &ebx, &ecx, &edx);
        if (ebx & (1 << 0))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "fsgsbase ");
        if (ebx & (1 << 3))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "bmi1 ");
        if (ebx & (1 << 4))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "hle ");
        if (ebx & (1 << 5))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx2 ");
        if (ebx & (1 << 7))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "smep ");
        if (ebx & (1 << 8))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "bmi2 ");
        if (ebx & (1 << 9))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "erms ");
        if (ebx & (1 << 10)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "invpcid ");
        if (ebx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rtm ");
        if (ebx & (1 << 14)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "mpx ");
        if (ebx & (1 << 16)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512f ");
        if (ebx & (1 << 17)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512dq ");
        if (ebx & (1 << 18)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdseed ");
        if (ebx & (1 << 19)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "adx ");
        if (ebx & (1 << 20)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "smap ");
        if (ebx & (1 << 21)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_ifma ");
        if (ebx & (1 << 23)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "clflushopt ");
        if (ebx & (1 << 24)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "clwb ");
        if (ebx & (1 << 29)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "sha_ni ");
        if (ebx & (1 << 30)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512bw ");
        if (ebx & (1 << 31)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512vl ");

        if (ecx & (1 << 1))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vbmi ");
        if (ecx & (1 << 6))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vbmi2 ");
        if (ecx & (1 << 8))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "gfni ");
        if (ecx & (1 << 9))  str_append(cpu_info.flags, sizeof(cpu_info.flags), "vaes ");
        if (ecx & (1 << 10)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "vpclmulqdq ");
        if (ecx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vnni ");
        if (ecx & (1 << 12)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_bitalg ");
        if (ecx & (1 << 14)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "avx512_vpopcntdq ");

        if (edx & (1 << 22)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_bf16 ");
        if (edx & (1 << 24)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_tile ");
        if (edx & (1 << 25)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "amx_int8 ");
    }

    /* 4. Extended Leafs: Brand string, Cache, Address Bits */
    x86_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    uint32_t max_ext = eax;
    if (max_ext >= 0x80000004) {
        uint32_t *brand_words = (uint32_t *)cpu_info.brand;
        x86_cpuid(0x80000002, &brand_words[0], &brand_words[1], &brand_words[2], &brand_words[3]);
        x86_cpuid(0x80000003, &brand_words[4], &brand_words[5], &brand_words[6], &brand_words[7]);
        x86_cpuid(0x80000004, &brand_words[8], &brand_words[9], &brand_words[10], &brand_words[11]);
        cpu_info.brand[48] = '\0';
    } else {
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "x86 Processor");
    }

    if (max_ext >= 0x80000006) {
        x86_cpuid(0x80000006, &eax, &ebx, &ecx, &edx);
        cpu_info.cache_size_kb = (ecx >> 16);
    }
    if (cpu_info.cache_size_kb == 0) cpu_info.cache_size_kb = 512;

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

    if (max_ext >= 0x80000001) {
        x86_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 11)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "syscall ");
        if (edx & (1 << 20)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "nx ");
        if (edx & (1 << 26)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "1gb_pages ");
        if (edx & (1 << 27)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "rdtscp ");
        if (edx & (1 << 29)) str_append(cpu_info.flags, sizeof(cpu_info.flags), "lm ");
    }

    cpu_info.mhz = 2600;
    cpu_info.bogomips_int = 5200;
    cpu_info.bogomips_frac = 0;

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
    cpu_info.cache_size_kb = 2048;
    cpu_info.cache_line_bytes = 64;
    cpu_info.phys_addr_bits = 40;
    cpu_info.virt_addr_bits = 48;

    if (implementer == 0x41) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "ARM");
        switch (part) {
        case 0xd03: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A53"); break;
        case 0xd07: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A57"); break;
        case 0xd08: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A72"); break;
        case 0xd09: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A73"); break;
        case 0xd0a: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A75"); break;
        case 0xd0b: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A76"); break;
        case 0xd0c: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Neoverse N1"); break;
        case 0xd40: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Neoverse V1"); break;
        case 0xd44: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-X1"); break;
        case 0xd48: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-X2"); break;
        case 0xd49: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Neoverse N2"); break;
        case 0xd4c: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-X3"); break;
        case 0xd4e: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-X4"); break;
        case 0xd82: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-X925"); break;
        default:    str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARMv8-A Processor"); break;
        }
    } else if (implementer == 0x51) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "Qualcomm");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "Qualcomm Snapdragon (Oryon/Kryo)");
    } else if (implementer == 0x61) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "Apple");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "Apple Silicon (M-Series / A-Series)");
    } else if (implementer == 0x42) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "Broadcom");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "Broadcom BCM2711 / BCM2712 (Raspberry Pi)");
    } else if (implementer == 0x53) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "Samsung");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "Samsung Exynos Processor");
    } else {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "ARM Compatible");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "AArch64 Processor");
    }
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
    cpu_info.cache_size_kb = 512;
    cpu_info.cache_line_bytes = 64;
    cpu_info.phys_addr_bits = 32;
    cpu_info.virt_addr_bits = 32;

    if (implementer == 0x41) {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "ARM");
        switch (part) {
        case 0xc07: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A7 (Allwinner/MediaTek)"); break;
        case 0xc08: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A8 (TI OMAP / Allwinner A10)"); break;
        case 0xc09: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A9 (Rockchip RK3066/RK3188)"); break;
        case 0xc0f: str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARM Cortex-A15 (Samsung Exynos 5)"); break;
        default:    str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARMv7 Processor [Cortex-A7]"); break;
        }
    } else {
        str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "ARM");
        str_append(cpu_info.brand, sizeof(cpu_info.brand), "ARMv7 Processor");
    }
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt");

#elif defined(__riscv)
    str_append(cpu_info.vendor, sizeof(cpu_info.vendor), "RISC-V");
#if __riscv_xlen == 64
    str_append(cpu_info.brand, sizeof(cpu_info.brand), "SiFive U74 / T-Head C906 / QEMU RV64GC");
    cpu_info.family = 64;
    cpu_info.phys_addr_bits = 39;
    cpu_info.virt_addr_bits = 39;
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "rv64imafdc zicsr zifencei zicbom zicbop zicboz v b");
#else
    str_append(cpu_info.brand, sizeof(cpu_info.brand), "SiFive E31 / QEMU RV32IMAC");
    cpu_info.family = 32;
    cpu_info.phys_addr_bits = 32;
    cpu_info.virt_addr_bits = 32;
    str_append(cpu_info.flags, sizeof(cpu_info.flags), "rv32imac zicsr zifencei v b");
#endif
    cpu_info.model = 0;
    cpu_info.stepping = 0;
    cpu_info.mhz = 1000;
    cpu_info.bogomips_int = 2000;
    cpu_info.cache_size_kb = 512;
    cpu_info.cache_line_bytes = 64;
#endif

    cpu_initialized = 1;
    xscore_serial_write("[INFO] CPU driver initialized: ");
    xscore_serial_write(cpu_info.brand);
    xscore_serial_write("\n");
}

const struct xscore_cpu_info *xscore_cpu_get_info(void) {
    if (!cpu_initialized) xscore_cpu_initialize();
    return &cpu_info;
}

static size_t app_str(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t app_u32(char *buf, size_t max, size_t off, uint32_t val) {
    char tmp[12];
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
        off = app_str(buf, max_size, off, ".000\ncache size\t: ");
        off = app_u32(buf, max_size, off, cpu_info.cache_size_kb);
        off = app_str(buf, max_size, off, " KB\nphysical id\t: 0\nsiblings\t: ");
        off = app_u32(buf, max_size, off, num_cpus);
        off = app_str(buf, max_size, off, "\ncore id\t\t: ");
        off = app_u32(buf, max_size, off, p);
        off = app_str(buf, max_size, off, "\ncpu cores\t: ");
        off = app_u32(buf, max_size, off, num_cpus);
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
        off = app_str(buf, max_size, off, " bits virtual\npower management:\n");
    }
    return off;
}
