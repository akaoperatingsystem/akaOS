
#ifndef XSCORE_IO_H
#define XSCORE_IO_H

#include <stdint.h>

#if defined(__x86_64__) || defined(__i386__)
static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t port_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t port_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
#else
/* Architecture stubs for non-x86 */
static inline uint8_t port_in8(uint16_t port) { (void)port; return 0; }
static inline void port_out8(uint16_t port, uint8_t value) { (void)port; (void)value; }
static inline uint16_t port_in16(uint16_t port) { (void)port; return 0; }
static inline void port_out16(uint16_t port, uint16_t value) { (void)port; (void)value; }
static inline uint32_t port_in32(uint16_t port) { (void)port; return 0; }
static inline void port_out32(uint16_t port, uint32_t value) { (void)port; (void)value; }
#endif

#endif /* XSCORE_IO_H */
