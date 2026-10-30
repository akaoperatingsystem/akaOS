#include <errno.h>
#include <akaos/syscall.h>
#include <akaos/futex.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

int sem_init(sem_t *semaphore, int pshared, unsigned int value) {
    if (!semaphore || (pshared != 0 && pshared != 1))
        return EINVAL;
    __atomic_store_n(&semaphore->value, (uint32_t)value, __ATOMIC_RELAXED);
    __atomic_store_n(&semaphore->sequence, 0U, __ATOMIC_RELAXED);
    semaphore->pshared = (uint32_t)pshared;
    return 0;
}

int sem_destroy(sem_t *semaphore) {
    if (!semaphore) return EINVAL;
    /* Waiters are cooperative and have no separate kernel object to free. */
    __atomic_store_n(&semaphore->sequence, 0U, __ATOMIC_RELAXED);
    return 0;
}

int sem_trywait(sem_t *semaphore) {
    if (!semaphore) return EINVAL;
    uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    for (;;) {
        if (value == 0U) return EAGAIN;
        uint32_t desired = value - 1U;
        if (__atomic_compare_exchange_n(&semaphore->value, &value, desired, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
    }
}

int sem_wait(sem_t *semaphore) {
    if (!semaphore) return EINVAL;
    for (;;) {
        if (sem_trywait(semaphore) == 0) return 0;
        uint32_t sequence =
            __atomic_load_n(&semaphore->sequence, __ATOMIC_ACQUIRE);
        if (sem_trywait(semaphore) == 0) return 0;
        long result = akaos_futex(
            &semaphore->sequence,
            akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                  semaphore->pshared != 0U),
            sequence);
        if (result < 0 && result != -EAGAIN) return (int)-result;
    }
}

int sem_timedwait(sem_t *semaphore, const struct timespec *abstime) {
    if (!semaphore || !abstime || abstime->tv_sec < 0 ||
        abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    for (;;) {
        if (sem_trywait(semaphore) == 0) return 0;
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) < 0) return EINVAL;
        if (now.tv_sec > abstime->tv_sec ||
            (now.tv_sec == abstime->tv_sec && now.tv_nsec >= abstime->tv_nsec))
            return ETIMEDOUT;
        struct timespec remaining = {
            .tv_sec = abstime->tv_sec - now.tv_sec,
            .tv_nsec = abstime->tv_nsec - now.tv_nsec
        };
        if (remaining.tv_nsec < 0) {
            --remaining.tv_sec;
            remaining.tv_nsec += 1000000000L;
        }
        uint32_t sequence =
            __atomic_load_n(&semaphore->sequence, __ATOMIC_ACQUIRE);
        if (sem_trywait(semaphore) == 0) return 0;
        long waited = akaos_futex_timed(
            &semaphore->sequence,
            akaos_futex_operation(AKAOS_FUTEX_WAIT_TIMEOUT,
                                  semaphore->pshared != 0U),
            sequence, &remaining);
        if (waited == -ETIMEDOUT) {
            if (sem_trywait(semaphore) == 0) return 0;
            return ETIMEDOUT;
        }
        if (waited < 0 && waited != -EAGAIN) return (int)-waited;
    }
}

int sem_post(sem_t *semaphore) {
    if (!semaphore) return EINVAL;
    uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    for (;;) {
        if (value == UINT32_MAX) return EOVERFLOW;
        uint32_t desired = value + 1U;
        if (__atomic_compare_exchange_n(&semaphore->value, &value, desired, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            (void)__atomic_add_fetch(&semaphore->sequence, 1U,
                                     __ATOMIC_RELEASE);
            (void)akaos_futex(
                &semaphore->sequence,
                akaos_futex_operation(AKAOS_FUTEX_WAKE,
                                      semaphore->pshared != 0U),
                1U);
            return 0;
        }
    }
}

int sem_getvalue(const sem_t *semaphore, int *value) {
    if (!semaphore || !value) return EINVAL;
    uint32_t current = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    *value = current > (uint32_t)INT32_MAX ? INT32_MAX : (int)current;
    return 0;
}
