#include <sys/timerfd.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>

/* Keep timerfd handles after the epoll (1024..1027), eventfd (1032..1063),
 * and eventfd-alias (1064..1079) ranges. Each object has a
 * native pipe backing it; the worker writes one marker when the expiration
 * counter changes from zero, and poll/epoll translate the public descriptor
 * back to the pipe's read end. */
#define AKAOS_TIMERFD_BASE 1080
#define AKAOS_TIMERFD_CAPACITY 4U
#define TIMERFD_MAX_NS UINT64_C(0xffffffffffffffff)

struct akaos_timerfd_object {
    int used;
    int descriptor;
    int read_fd;
    int write_fd;
    int flags;
    int clock_id;
    uint32_t lock;
    uint32_t worker_started;
    uint32_t closed;
    uint64_t next_deadline_ns;
    uint64_t interval_ns;
    uint64_t expirations;
    pthread_t worker;
};

static struct akaos_timerfd_object objects[AKAOS_TIMERFD_CAPACITY];

int __akaos_timerfd_close(int descriptor);

/* The freestanding ARM32/RISC-V32 links intentionally do not pull in a
 * compiler runtime.  Keep all 64-bit quotient/remainder operations in this
 * small shift/subtract helper instead of emitting __aeabi_uldivmod or
 * __udivdi3 references. */
static void timer_divmod_u64(uint64_t numerator, uint64_t denominator,
                             uint64_t *quotient, uint64_t *remainder) {
    uint64_t q = 0;
    uint64_t r = 0;
    if (denominator != 0) {
        for (int bit = 63; bit >= 0; --bit) {
            r = (r << 1) | ((numerator >> bit) & 1U);
            if (r >= denominator) {
                r -= denominator;
                q |= UINT64_C(1) << bit;
            }
        }
    }
    if (quotient) *quotient = q;
    if (remainder) *remainder = r;
}

static uint64_t timer_div_u64(uint64_t numerator, uint64_t denominator) {
    uint64_t quotient;
    timer_divmod_u64(numerator, denominator, &quotient, NULL);
    return quotient;
}

static uint64_t timer_mod_u64(uint64_t numerator, uint64_t denominator) {
    uint64_t remainder;
    timer_divmod_u64(numerator, denominator, NULL, &remainder);
    return remainder;
}

static void timer_lock(struct akaos_timerfd_object *object) {
    uint32_t expected;
    do {
        expected = 0;
    } while (!__atomic_compare_exchange_n(&object->lock, &expected, 1U, 0,
                                          __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
}

static void timer_unlock(struct akaos_timerfd_object *object) {
    __atomic_store_n(&object->lock, 0U, __ATOMIC_RELEASE);
}

static struct akaos_timerfd_object *find_timerfd(int descriptor) {
    if (descriptor < AKAOS_TIMERFD_BASE ||
        descriptor >= AKAOS_TIMERFD_BASE + (int)AKAOS_TIMERFD_CAPACITY)
        return NULL;
    struct akaos_timerfd_object *object =
        &objects[descriptor - AKAOS_TIMERFD_BASE];
    return object->used && object->descriptor == descriptor ? object : NULL;
}

static int timespec_to_ns(const struct timespec *value, uint64_t *result) {
    if (!value || value->tv_sec < 0 || value->tv_nsec < 0 ||
        value->tv_nsec >= 1000000000L || !result)
        return 0;
    uint64_t seconds = (uint64_t)value->tv_sec;
    if (seconds > timer_div_u64(TIMERFD_MAX_NS -
                                (uint64_t)value->tv_nsec,
                                UINT64_C(1000000000))) return 0;
    *result = seconds * UINT64_C(1000000000) + (uint64_t)value->tv_nsec;
    return 1;
}

static int timer_now_ns(int clock_id, uint64_t *result) {
    struct timespec now;
    if (clock_gettime(clock_id, &now) < 0) return 0;
    return timespec_to_ns(&now, result);
}

static void ns_to_timespec(uint64_t value, struct timespec *result) {
    result->tv_sec = (time_t)timer_div_u64(value, UINT64_C(1000000000));
    result->tv_nsec = (long)timer_mod_u64(value, UINT64_C(1000000000));
}

static int timer_signal(struct akaos_timerfd_object *object) {
    uint8_t marker = 1;
    return write(object->write_fd, &marker, sizeof(marker)) ==
           (ssize_t)sizeof(marker);
}

static void *timerfd_worker(void *argument) {
    struct akaos_timerfd_object *object =
        (struct akaos_timerfd_object *)argument;
    for (;;) {
        timer_lock(object);
        if (object->closed) {
            object->used = 0;
            object->worker_started = 0;
            timer_unlock(object);
            return NULL;
        }
        uint64_t deadline = object->next_deadline_ns;
        int clock_id = object->clock_id;
        timer_unlock(object);

        if (deadline == 0) {
            (void)poll(NULL, 0, 10);
            continue;
        }
        uint64_t now = 0;
        if (!timer_now_ns(clock_id, &now)) {
            (void)poll(NULL, 0, 10);
            continue;
        }
        if (now < deadline) {
            uint64_t remaining = deadline - now;
            uint64_t milliseconds = timer_div_u64(
                remaining + UINT64_C(999999), UINT64_C(1000000));
            if (milliseconds == 0) milliseconds = 1;
            if (milliseconds > 1000) milliseconds = 1000;
            (void)poll(NULL, 0, (int)milliseconds);
            continue;
        }

        timer_lock(object);
        if (object->closed) {
            timer_unlock(object);
            continue;
        }
        /* The timer may have been reset while clock_gettime ran. */
        if (object->next_deadline_ns == 0) {
            timer_unlock(object);
            continue;
        }
        if (!timer_now_ns(object->clock_id, &now) ||
            now < object->next_deadline_ns) {
            timer_unlock(object);
            continue;
        }
        uint64_t count = 1;
        if (object->interval_ns != 0) {
            count += timer_div_u64(now - object->next_deadline_ns,
                                   object->interval_ns);
            uint64_t advance = count * object->interval_ns;
            if (count != 0 && count > timer_div_u64(TIMERFD_MAX_NS,
                                                    object->interval_ns))
                object->next_deadline_ns = 0;
            else if (object->next_deadline_ns > TIMERFD_MAX_NS - advance)
                object->next_deadline_ns = 0;
            else
                object->next_deadline_ns += advance;
        } else {
            object->next_deadline_ns = 0;
        }
        int was_zero = object->expirations == 0;
        if (TIMERFD_MAX_NS - object->expirations < count)
            object->expirations = TIMERFD_MAX_NS;
        else
            object->expirations += count;
        if (was_zero && object->expirations != 0)
            (void)timer_signal(object);
        timer_unlock(object);
    }
}

static int ensure_worker(struct akaos_timerfd_object *object) {
    timer_lock(object);
    if (object->worker_started) {
        timer_unlock(object);
        return 1;
    }
    object->worker_started = 1;
    timer_unlock(object);

    pthread_t worker = 0;
    int result = pthread_create(&worker, NULL, timerfd_worker, object);
    if (result != 0) {
        timer_lock(object);
        object->worker_started = 0;
        timer_unlock(object);
        errno = result;
        return 0;
    }
    timer_lock(object);
    object->worker = worker;
    timer_unlock(object);
    return 1;
}

int timerfd_create(int clock_id, int flags) {
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    if (flags & ~(TFD_CLOEXEC | TFD_NONBLOCK)) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < AKAOS_TIMERFD_CAPACITY; ++index) {
        struct akaos_timerfd_object *object = &objects[index];
        timer_lock(object);
        if (object->used) {
            timer_unlock(object);
            continue;
        }
        int descriptors[2];
        if (pipe(descriptors) < 0) {
            timer_unlock(object);
            return -1;
        }
        object->used = 1;
        object->descriptor = AKAOS_TIMERFD_BASE + (int)index;
        object->read_fd = descriptors[0];
        object->write_fd = descriptors[1];
        object->flags = flags;
        object->clock_id = clock_id;
        object->closed = 0;
        object->worker_started = 0;
        object->next_deadline_ns = 0;
        object->interval_ns = 0;
        object->expirations = 0;
        object->worker = 0;
        timer_unlock(object);
        if (!ensure_worker(object)) {
            (void)__akaos_timerfd_close(object->descriptor);
            return -1;
        }
        return object->descriptor;
    }
    errno = EMFILE;
    return -1;
}

static void fill_old_value(struct akaos_timerfd_object *object,
                            struct itimerspec *value) {
    if (!value) return;
    uint64_t now = 0;
    uint64_t remaining = 0;
    if (object->next_deadline_ns != 0 && timer_now_ns(object->clock_id, &now) &&
        now < object->next_deadline_ns)
        remaining = object->next_deadline_ns - now;
    ns_to_timespec(remaining, &value->it_value);
    ns_to_timespec(object->interval_ns, &value->it_interval);
}

int timerfd_settime(int descriptor, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value) {
    struct akaos_timerfd_object *object = find_timerfd(descriptor);
    if (!object || !new_value || (flags & ~TFD_TIMER_ABSTIME)) {
        errno = EINVAL;
        return -1;
    }
    uint64_t interval = 0;
    uint64_t value = 0;
    if (!timespec_to_ns(&new_value->it_interval, &interval) ||
        !timespec_to_ns(&new_value->it_value, &value)) {
        errno = EINVAL;
        return -1;
    }
    uint64_t now = 0;
    if (value != 0 && !(flags & TFD_TIMER_ABSTIME)) {
        if (!timer_now_ns(object->clock_id, &now) ||
            now > TIMERFD_MAX_NS - value) {
            errno = ERANGE;
            return -1;
        }
        value += now;
    }

    timer_lock(object);
    if (object->closed) {
        timer_unlock(object);
        errno = EBADF;
        return -1;
    }
    fill_old_value(object, old_value);
    /* Resetting a timer discards unread expirations.  There is at most one
     * marker in the pipe, because the worker signals only on a zero->nonzero
     * transition. */
    if (object->expirations != 0) {
        uint8_t marker;
        (void)read(object->read_fd, &marker, sizeof(marker));
    }
    object->expirations = 0;
    object->interval_ns = interval;
    object->next_deadline_ns = value;
    timer_unlock(object);
    return 0;
}

int timerfd_gettime(int descriptor, struct itimerspec *current_value) {
    struct akaos_timerfd_object *object = find_timerfd(descriptor);
    if (!object || !current_value) {
        errno = EINVAL;
        return -1;
    }
    timer_lock(object);
    if (object->closed) {
        timer_unlock(object);
        errno = EBADF;
        return -1;
    }
    fill_old_value(object, current_value);
    timer_unlock(object);
    return 0;
}

/* Returns 1 when a virtual timerfd consumed the operation. */
int __akaos_timerfd_read(int descriptor, void *buffer, size_t length,
                         ssize_t *result) {
    struct akaos_timerfd_object *object = find_timerfd(descriptor);
    if (!object) return 0;
    if (!buffer || length < sizeof(uint64_t)) {
        errno = EINVAL;
        *result = -1;
        return 1;
    }
    for (;;) {
        timer_lock(object);
        if (object->closed) {
            timer_unlock(object);
            errno = EBADF;
            *result = -1;
            return 1;
        }
        if (object->expirations != 0) {
            uint8_t marker;
            ssize_t consumed = read(object->read_fd, &marker, sizeof(marker));
            if (consumed != (ssize_t)sizeof(marker)) {
                timer_unlock(object);
                *result = -1;
                return 1;
            }
            uint64_t value = object->expirations;
            object->expirations = 0;
            timer_unlock(object);
            *(uint64_t *)buffer = value;
            *result = sizeof(uint64_t);
            return 1;
        }
        int nonblock = (object->flags & TFD_NONBLOCK) != 0;
        int read_fd = object->read_fd;
        timer_unlock(object);
        if (nonblock) {
            errno = EAGAIN;
            *result = -1;
            return 1;
        }
        struct pollfd descriptor = {read_fd, POLLIN, 0};
        if (poll(&descriptor, 1, -1) < 0) {
            *result = -1;
            return 1;
        }
    }
}

int __akaos_timerfd_write(int descriptor, const void *buffer, size_t length,
                          ssize_t *result) {
    if (!find_timerfd(descriptor)) return 0;
    (void)buffer;
    (void)length;
    errno = EINVAL;
    *result = -1;
    return 1;
}

int __akaos_timerfd_poll_fd(int descriptor, short events) {
    struct akaos_timerfd_object *object = find_timerfd(descriptor);
    if (!object) return descriptor;
    timer_lock(object);
    int native = (events & POLLOUT) ? object->write_fd : object->read_fd;
    timer_unlock(object);
    return native;
}

int __akaos_timerfd_close(int descriptor) {
    struct akaos_timerfd_object *object = find_timerfd(descriptor);
    if (!object) return 0;
    timer_lock(object);
    int read_fd = object->read_fd;
    int write_fd = object->write_fd;
    pthread_t worker = object->worker;
    int worker_started = object->worker_started != 0;
    object->descriptor = -1;
    object->read_fd = -1;
    object->write_fd = -1;
    object->closed = 1;
    timer_unlock(object);
    (void)close(read_fd);
    (void)close(write_fd);
    /* The worker is joinable deliberately.  A detached timer worker could
     * outlive the process's main thread and execute another instruction after
     * XSCore marked the process exited, which is not a valid process lifetime
     * on the current shared-address-space thread ABI. */
    if (worker_started && worker != 0)
        (void)pthread_join(worker, NULL);
    timer_lock(object);
    object->used = 0;
    object->worker_started = 0;
    object->closed = 0;
    object->worker = 0;
    timer_unlock(object);
    return 1;
}

int __akaos_timerfd_descriptor_valid(int descriptor) {
    return find_timerfd(descriptor) != NULL;
}

/* Called by libc _exit so a program that leaves a timerfd open cannot leave a
 * userspace worker running past process teardown. */
void __akaos_timerfd_shutdown(void) {
    for (size_t index = 0; index < AKAOS_TIMERFD_CAPACITY; ++index) {
        timer_lock(&objects[index]);
        int descriptor = objects[index].used ? objects[index].descriptor : -1;
        timer_unlock(&objects[index]);
        if (descriptor >= 0) (void)__akaos_timerfd_close(descriptor);
    }
}
