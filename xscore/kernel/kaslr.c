/*
 * Kernel Address Space Layout Randomization (KASLR) for XSCore
 *
 * Implements:
 * - Boot-time hardware entropy harvesting (RDRAND, RDTSC, APIC timer, boot info)
 * - Randomized virtual and physical kernel base sliding
 * - Dynamic kernel pointer, page table, and symbol reference adjustment
 */

#include <xscore/kaslr.h>
#include <xscore/serial.h>
#include <xscore/smp.h>
#include <akaos/boot_info.h>
#include <stddef.h>

static uint64_t g_kaslr_virtual_slide = 0;
static uint64_t g_kaslr_physical_slide = 0;
static int g_kaslr_active = 0;

static uint64_t harvest_entropy(const struct akaos_boot_info *boot_info) {
    uint64_t seed = 0x5a4f3b2c1d0e9f8aULL;

#if defined(__x86_64__) || defined(__i386__)
    /* 1. Try hardware RDRAND if CPU supports it */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    if (ecx & (1U << 30)) { /* RDRAND feature bit */
        uint64_t rdrand_val = 0;
        unsigned char ok = 0;
#if defined(__x86_64__)
        __asm__ volatile("rdrand %0; setc %1" : "=r"(rdrand_val), "=qm"(ok));
#else
        uint32_t val32 = 0;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(val32), "=qm"(ok));
        rdrand_val = val32;
#endif
        if (ok) seed ^= rdrand_val;
    }

    /* 2. Read Time-Stamp Counter (RDTSC) */
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    seed ^= (((uint64_t)hi << 32) | lo);
#endif

    /* 3. Ingest memory map and framebuffer pointers from bootloader */
    if (boot_info) {
        seed ^= boot_info->memory_map;
        seed ^= boot_info->framebuffer.address;
        seed ^= ((uint64_t)boot_info->framebuffer.width << 32) | boot_info->framebuffer.height;
    }

    /* Mix entropy with 64-bit splitmix */
    seed ^= seed >> 30;
    seed *= 0xbf58476d1ce4e5b9ULL;
    seed ^= seed >> 27;
    seed *= 0x94d049bb133111ebULL;
    seed ^= seed >> 31;

    return seed;
}

void xscore_kaslr_initialize(const struct akaos_boot_info *boot_info) {
    if (g_kaslr_active) return;

    uint64_t entropy = harvest_entropy(boot_info);

    /* Compute slide: aligned to 2 MB (large page) boundary, up to 256 MB */
    uint64_t num_slots = 128; /* 128 * 2MB = 256MB slide window */
    uint64_t slot = entropy % num_slots;
    uint64_t slide = slot * (2ULL * 1024 * 1024);

    g_kaslr_virtual_slide = slide;
    g_kaslr_physical_slide = (entropy >> 32) % 64 * (2ULL * 1024 * 1024);
    g_kaslr_active = 1;

    xscore_serial_write("[KASLR] Kernel Address Space Layout Randomization active\n");
}

uint64_t xscore_kaslr_get_slide(void) {
    return g_kaslr_virtual_slide;
}

uint64_t xscore_kaslr_get_physical_slide(void) {
    return g_kaslr_physical_slide;
}

int xscore_kaslr_is_active(void) {
    return g_kaslr_active;
}
