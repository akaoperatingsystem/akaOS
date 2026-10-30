#ifndef XSCORE_TIMER_H
#define XSCORE_TIMER_H

#include <stdint.h>

/*
 * Default timer frequency in Hz.
 * The APIC timer is calibrated against the PIT and configured
 * to fire periodic interrupts at this rate.
 */
#define XSCORE_TIMER_DEFAULT_HZ 100

/*
 * Initialize the APIC timer.
 * Calibrates against PIT channel 2 and starts periodic interrupts.
 * Must be called after xscore_apic_initialize().
 * Returns 1 on success, 0 on failure.
 */
int xscore_timer_initialize(void);
void xscore_timer_ap_initialize(void);

/*
 * Read the monotonic tick counter.
 * Incremented once per timer interrupt.
 * Starts at 0 and wraps at UINT64_MAX.
 */
uint64_t xscore_timer_ticks(void);

/*
 * Return the configured timer frequency in Hz.
 */
uint32_t xscore_timer_frequency(void);

/*
 * Called by the timer ISR. Not for external use.
 */
void xscore_timer_interrupt(void);

#endif
