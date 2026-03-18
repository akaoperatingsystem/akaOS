/* ============================================================
 * akaOS — I/O Port Helper Functions (Multi-Architecture)
 * ============================================================
 * Provides outb/inb/outw/outl/inl for x86 architectures.
 * ARM64 gets no-op stubs (ARM uses MMIO, not port I/O).
 *
 * The correct implementation is selected at compile-time via
 * the ARCH_* macro defined by the Makefile (-DARCH_X86_64 etc).
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

#if defined(ARCH_X86_64) || defined(ARCH_X86_32)

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Write a word to an I/O port */
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Write a dword to an I/O port */
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Read a dword from an I/O port */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Wait for an I/O operation to complete (tiny delay) */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#elif defined(ARCH_AARCH64)

/* ARM64: no port I/O — these are no-op stubs for source compat */
static inline void outb(uint16_t port, uint8_t val)   { (void)port; (void)val; }
static inline void outw(uint16_t port, uint16_t val)   { (void)port; (void)val; }
static inline void outl(uint16_t port, uint32_t val)   { (void)port; (void)val; }
static inline uint8_t  inb(uint16_t port) { (void)port; return 0xFF; }
static inline uint32_t inl(uint16_t port) { (void)port; return 0xFFFFFFFF; }
static inline void io_wait(void) {}

/* MMIO helpers for ARM64 */
static inline void     mmio_write32(volatile uint32_t *addr, uint32_t val) { *addr = val; }
static inline uint32_t mmio_read32(volatile uint32_t *addr)  { return *addr; }

#else
#error "Unknown architecture — define ARCH_X86_64, ARCH_X86_32, or ARCH_AARCH64"
#endif

#endif /* IO_H */
