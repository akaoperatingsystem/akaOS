/* ============================================================
 * akaOS — x86_32 Architecture Definitions
 * ============================================================ */

#ifndef ARCH_H
#define ARCH_H

#define ARCH_X86_32  1
#define ARCH_HAS_PIO 1
#define ARCH_BITS    32

static inline void arch_halt(void) {
    asm volatile("hlt");
}

static inline void arch_cli(void) {
    asm volatile("cli");
}

static inline void arch_sti(void) {
    asm volatile("sti");
}

static inline void arch_halt_forever(void) {
    asm volatile("cli");
    while (1) asm volatile("hlt");
}

/* x86 RDTSC — read timestamp counter */
static inline unsigned long long arch_rdtsc(void) {
    unsigned int lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

#endif /* ARCH_H */
