#ifndef XSCORE_SERIAL_H
#define XSCORE_SERIAL_H

#include <stdint.h>

/*
 * XSCore serial console (COM1 at 115200 baud).
 *
 * These routines are safe to call as soon as the kernel is entered; the
 * port is configured by the UEFI loader, which uses the same settings.
 * They busy-wait on the UART transmitter holding register, so they are
 * interrupt-safe but intentionally slow.
 */

void xscore_serial_write(const char *text);

/* Bounded variant for syscall buffers, which are not required to be NUL
 * terminated. The whole buffer is emitted as one serial transaction. */
void xscore_serial_write_n(const char *text, uint64_t length);

size_t xscore_kernel_log_read(char *buffer, size_t max_len, int clear);

#endif
