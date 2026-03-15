/* ============================================================
 * akaOS — PIT Timer Driver Implementation
 * ============================================================
 * Configures PIT Channel 0 at ~100 Hz for uptime tracking.
 */

#include "time.h"
#include "idt.h"
#include "io.h"
#include "string.h"

/* PIT base frequency */
#define PIT_FREQUENCY 1193180

/* Target frequency: 100 Hz (10 ms per tick) */
#define TARGET_HZ 100
#define PIT_DIVISOR (PIT_FREQUENCY / TARGET_HZ)

static volatile uint64_t tick_count = 0;

static void timer_irq_handler(struct regs *r) {
    (void)r;
    tick_count++;
}

void timer_init(void) {
    /* Set PIT Channel 0, lobyte/hibyte mode, square wave (mode 3) */
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(PIT_DIVISOR & 0xFF));
    outb(0x40, (uint8_t)((PIT_DIVISOR >> 8) & 0xFF));

    /* Install IRQ0 handler */
    irq_install_handler(0, timer_irq_handler);
}

uint64_t timer_get_ticks(void) {
    return tick_count;
}

uint64_t timer_get_seconds(void) {
    return tick_count / TARGET_HZ;
}

void timer_format_uptime(char *buf, int buf_size) {
    uint64_t total_secs = timer_get_seconds();
    uint64_t hours   = total_secs / 3600;
    uint64_t minutes = (total_secs % 3600) / 60;
    uint64_t seconds = total_secs % 60;

    char h[12], m[12], s[12];
    int_to_str((int)hours, h);
    int_to_str((int)minutes, m);
    int_to_str((int)seconds, s);

    /* Format: "Xh Ym Zs" */
    int pos = 0;
    for (int i = 0; h[i] && pos < buf_size - 1; i++) buf[pos++] = h[i];
    if (pos < buf_size - 1) buf[pos++] = 'h';
    if (pos < buf_size - 1) buf[pos++] = ' ';
    for (int i = 0; m[i] && pos < buf_size - 1; i++) buf[pos++] = m[i];
    if (pos < buf_size - 1) buf[pos++] = 'm';
    if (pos < buf_size - 1) buf[pos++] = ' ';
    for (int i = 0; s[i] && pos < buf_size - 1; i++) buf[pos++] = s[i];
    if (pos < buf_size - 1) buf[pos++] = 's';
    buf[pos] = '\0';
}
