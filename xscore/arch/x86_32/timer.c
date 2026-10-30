#include <xscore/timer.h>
#include <xscore/apic.h>
#include <xscore/sched.h>
#include <xscore/serial.h>
#include <stdint.h>

extern void xscore_32_outb(uint16_t port, uint8_t val);
extern uint8_t xscore_32_inb(uint16_t port);

static volatile uint64_t x86_32_timer_ticks = 0;

int xscore_apic_initialize(void) {
    /* 8259 PIC is remapped in tables.c (IRQ 0..15 -> vectors 32..47) */
    return 1;
}

void xscore_apic_eoi(void) {
    xscore_32_outb(0xA0, 0x20);
    xscore_32_outb(0x20, 0x20);
}

int xscore_irq_route_isa(uint8_t irq) {
    if (irq >= 16) return 0;
    uint16_t port = irq < 8 ? 0x21 : 0xa1;
    xscore_32_outb(port, xscore_32_inb(port) & ~(1U << (irq & 7)));
    if (irq >= 8) xscore_32_outb(0x21, xscore_32_inb(0x21) & ~(1U << 2));
    return 1;
}

uint32_t xscore_apic_id(void) {
    return 0;
}

int xscore_timer_initialize(void) {
    /* Configure 8254 PIT Channel 0 for 100 Hz (divisor 11932 = 1193182 / 100) */
    uint16_t divisor = 11932;
    xscore_32_outb(0x43, 0x36);
    xscore_32_outb(0x40, (uint8_t)(divisor & 0xFF));
    xscore_32_outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    return 1;
}

uint64_t xscore_timer_ticks(void) {
    return x86_32_timer_ticks;
}

uint32_t xscore_timer_frequency(void) {
    return XSCORE_TIMER_DEFAULT_HZ;
}

void xscore_timer_interrupt(void) {
    ++x86_32_timer_ticks;
    xscore_sched_timer_tick();
}
