#include <xscore/timer.h>
#include <xscore/apic.h>
#include <xscore/sched.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

/*
 * APIC timer register offsets (duplicated from apic.c for locality).
 */
#define APIC_REG_LVT_TIMER      0x320
#define APIC_REG_TIMER_INITIAL   0x380
#define APIC_REG_TIMER_CURRENT   0x390
#define APIC_REG_TIMER_DIVIDE    0x3E0

/* LVT timer mode bits */
#define APIC_TIMER_PERIODIC      (1U << 17)
#define APIC_TIMER_MASKED        (1U << 16)

/* PIT constants for calibration */
#define PIT_FREQUENCY            1193182U
#define PIT_CHANNEL2_DATA        0x42
#define PIT_COMMAND              0x43
#define PIT_GATE                 0x61

/* Calibration: measure APIC ticks over ~10 ms using PIT channel 2 */
#define CALIBRATION_PIT_COUNT    11932U  /* ~10 ms at 1.193182 MHz */

/* APIC base address — must match the one resolved in apic.c */
extern volatile uint32_t *apic_base;

static volatile uint64_t monotonic_ticks;
static uint32_t timer_hz;
static uint32_t apic_ticks_per_interrupt;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void apic_timer_write(uint32_t offset, uint32_t value) {
    apic_base[offset / 4] = value;
}

static uint32_t apic_timer_read(uint32_t offset) {
    return apic_base[offset / 4];
}

/*
 * Calibrate APIC timer frequency using PIT channel 2 as a reference.
 * PIT channel 2 is used because it doesn't require IRQ routing.
 *
 * Returns APIC timer ticks per second.
 */
static uint32_t calibrate_apic_timer(void) {
    /* Set APIC timer divider to 16 */
    apic_timer_write(APIC_REG_TIMER_DIVIDE, 0x03);

    /* Configure PIT channel 2 in one-shot mode */
    uint8_t gate = in8(PIT_GATE);
    gate = (gate & 0xFD) | 0x01; /* disable speaker, enable gate */
    out8(PIT_GATE, gate);

    /* Channel 2, mode 0 (interrupt on terminal count), binary */
    out8(PIT_COMMAND, 0xB0);
    out8(PIT_CHANNEL2_DATA, (uint8_t)(CALIBRATION_PIT_COUNT & 0xFF));
    out8(PIT_CHANNEL2_DATA, (uint8_t)(CALIBRATION_PIT_COUNT >> 8));

    /* Start APIC timer with maximum initial count */
    apic_timer_write(APIC_REG_TIMER_INITIAL, 0xFFFFFFFF);

    /* Wait for PIT channel 2 output to go high (bit 5 of port 0x61) */
    while ((in8(PIT_GATE) & 0x20) == 0) {
        __asm__ volatile("pause");
    }

    /* Read how many APIC ticks elapsed */
    uint32_t elapsed = 0xFFFFFFFF - apic_timer_read(APIC_REG_TIMER_CURRENT);

    /* Stop APIC timer */
    apic_timer_write(APIC_REG_TIMER_INITIAL, 0);

    /* elapsed ticks happened in ~10 ms, so ticks_per_second = elapsed * 100 */
    return elapsed * 100;
}

int xscore_timer_initialize(void) {
    uint32_t ticks_per_second = calibrate_apic_timer();
    if (ticks_per_second == 0) return 0;

    timer_hz = XSCORE_TIMER_DEFAULT_HZ;
    apic_ticks_per_interrupt = ticks_per_second / timer_hz;
    if (apic_ticks_per_interrupt == 0) apic_ticks_per_interrupt = 1;

    monotonic_ticks = 0;

    /* Set divider to 16 */
    apic_timer_write(APIC_REG_TIMER_DIVIDE, 0x03);

    /* Configure LVT timer: periodic mode, vector XSCORE_IRQ_TIMER */
    apic_timer_write(APIC_REG_LVT_TIMER,
                     APIC_TIMER_PERIODIC | XSCORE_IRQ_TIMER);

    /* Start periodic interrupts */
    apic_timer_write(APIC_REG_TIMER_INITIAL, apic_ticks_per_interrupt);

    return 1;
}

void xscore_timer_ap_initialize(void) {
    if (apic_ticks_per_interrupt == 0) return;
    apic_timer_write(APIC_REG_TIMER_DIVIDE, 0x03);
    apic_timer_write(APIC_REG_LVT_TIMER,
                     APIC_TIMER_PERIODIC | XSCORE_IRQ_TIMER);
    apic_timer_write(APIC_REG_TIMER_INITIAL, apic_ticks_per_interrupt);
}

uint64_t xscore_timer_ticks(void) {
    return monotonic_ticks;
}

uint32_t xscore_timer_frequency(void) {
    return timer_hz;
}

void xscore_timer_interrupt(void) {
    if (xscore_smp_is_bsp()) {
        ++monotonic_ticks;
    }
    xscore_sched_timer_tick();
    xscore_apic_eoi();
}
