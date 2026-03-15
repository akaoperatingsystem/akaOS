/* ============================================================
 * akaOS — PIT Timer Driver Header
 * ============================================================ */

#ifndef TIME_H
#define TIME_H

#include <stdint.h>

/* Initialize PIT timer at ~100 Hz (IRQ0) */
void timer_init(void);

/* Get total ticks since boot */
uint64_t timer_get_ticks(void);

/* Get seconds since boot */
uint64_t timer_get_seconds(void);

/* Format uptime into a human-readable string */
void timer_format_uptime(char *buf, int buf_size);

#endif /* TIME_H */
