#ifndef AKAOS_LIBC_SEMAPHORE_H
#define AKAOS_LIBC_SEMAPHORE_H

/* Bounded POSIX semaphore ABI for the freestanding akaOS libc. */

#include <stdint.h>
#include <time.h>

typedef struct {
    volatile uint32_t value;
    volatile uint32_t sequence;
    uint32_t pshared;
} sem_t;

#define SEM_FAILED ((sem_t *)(uintptr_t)0)

#ifdef __cplusplus
extern "C" {
#endif
int sem_init(sem_t *semaphore, int pshared, unsigned int value);
int sem_destroy(sem_t *semaphore);
int sem_wait(sem_t *semaphore);
int sem_trywait(sem_t *semaphore);
int sem_timedwait(sem_t *semaphore, const struct timespec *abstime);
int sem_post(sem_t *semaphore);
int sem_getvalue(const sem_t *semaphore, int *value);
#ifdef __cplusplus
}
#endif

#endif
