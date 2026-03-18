/* ============================================================
 * akaOS — AArch64 Architecture Definitions
 * ============================================================ */

#ifndef ARCH_H
#define ARCH_H

#define ARCH_AARCH64  1
#define ARCH_HAS_PIO  0
#define ARCH_BITS     64

static inline void arch_halt(void) {
    asm volatile("yield");
}

static inline void arch_cli(void) {
    asm volatile("msr daifset, #2");
}

static inline void arch_sti(void) {
    asm volatile("msr daifclr, #2");
}

static inline void arch_halt_forever(void) {
    asm volatile("msr daifset, #2");
    while (1) asm volatile("wfi");
}

/* ARM64 cycle counter (CNTVCT_EL0) */
static inline unsigned long long arch_rdtsc(void) {
    unsigned long long val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

#endif /* ARCH_H */
