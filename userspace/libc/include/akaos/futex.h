#ifndef AKAOS_LIBC_FUTEX_H
#define AKAOS_LIBC_FUTEX_H

#include <akaos/syscall.h>
#include <stdint.h>
#include <time.h>

/* Private libc/kernel contract for blocking synchronization operations. */
long akaos_futex(volatile uint32_t *word, uint32_t operation, uint32_t value);
long akaos_futex_timed(volatile uint32_t *word, uint32_t operation,
                       uint32_t expected, const struct timespec *timeout);

static inline uint32_t akaos_futex_operation(uint32_t operation,
                                            int process_shared) {
    return process_shared ? operation
                          : operation | AKAOS_FUTEX_PRIVATE_FLAG;
}

#endif
