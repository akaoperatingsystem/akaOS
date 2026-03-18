/* ============================================================
 * akaOS — I/O Helpers (AArch64 — MMIO Only)
 * ============================================================
 * ARM64 has no port I/O. All device access is memory-mapped.
 * These stubs provide compatibility with code that uses
 * x86-style outb/inb — they are no-ops on ARM64.
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

/* MMIO write helpers */
static inline void mmio_write32(volatile uint32_t *addr, uint32_t val) {
    *addr = val;
    asm volatile("dsb sy" ::: "memory");
}

static inline void mmio_write8(volatile uint8_t *addr, uint8_t val) {
    *addr = val;
    asm volatile("dsb sy" ::: "memory");
}

/* MMIO read helpers */
static inline uint32_t mmio_read32(volatile uint32_t *addr) {
    uint32_t val = *addr;
    asm volatile("dsb sy" ::: "memory");
    return val;
}

static inline uint8_t mmio_read8(volatile uint8_t *addr) {
    uint8_t val = *addr;
    asm volatile("dsb sy" ::: "memory");
    return val;
}

/* Compatibility stubs for x86 port I/O — NO-OPs on ARM64 */
static inline void outb(uint16_t port, uint8_t val) {
    (void)port; (void)val;
}

static inline void outw(uint16_t port, uint16_t val) {
    (void)port; (void)val;
}

static inline void outl(uint16_t port, uint32_t val) {
    (void)port; (void)val;
}

static inline uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}

static inline uint32_t inl(uint16_t port) {
    (void)port;
    return 0;
}

static inline void io_wait(void) {
    /* No-op on ARM64 */
}

#endif /* IO_H */
