/* ============================================================
 * akaOS — I/O Port Helper Functions (x86_32)
 * ============================================================
 * Inline assembly wrappers for x86 port I/O instructions.
 * Identical to x86_64 version — port I/O is the same in 32-bit.
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

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

#endif /* IO_H */
