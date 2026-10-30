#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define AKAOS_CXA_EXIT_MAX 64U

typedef void (*cxa_destructor_fn)(void *);

struct cxa_exit_record {
    cxa_destructor_fn destructor;
    void *argument;
    void *dso;
    uint64_t order;
    int active;
};

static struct cxa_exit_record exit_records[AKAOS_CXA_EXIT_MAX];
static uint32_t exit_record_lock;
static uint64_t next_exit_order = 1U;

/* The hidden handle is private to this linked image, as expected by the
 * Itanium C++ ABI. A separately linked DSO must provide its own handle. */
void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;

static void exit_table_acquire(void) {
    uint32_t expected;
    do {
        expected = 0U;
    } while (!__atomic_compare_exchange_n(&exit_record_lock, &expected, 1U,
                                          0, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
}

static void exit_table_release(void) {
    __atomic_store_n(&exit_record_lock, 0U, __ATOMIC_RELEASE);
}

int __cxa_atexit(cxa_destructor_fn destructor, void *argument, void *dso) {
    if (!destructor) {
        errno = EINVAL;
        return -1;
    }

    exit_table_acquire();
    for (size_t index = 0; index < AKAOS_CXA_EXIT_MAX; ++index) {
        struct cxa_exit_record *record = &exit_records[index];
        if (record->active) continue;
        record->destructor = destructor;
        record->argument = argument;
        record->dso = dso;
        record->order = next_exit_order++;
        if (next_exit_order == 0U) next_exit_order = 1U;
        record->active = 1;
        exit_table_release();
        return 0;
    }
    exit_table_release();
    errno = ENOMEM;
    return -1;
}

int atexit(void (*function)(void)) {
    if (!function) {
        errno = EINVAL;
        return -1;
    }
    return __cxa_atexit((cxa_destructor_fn)function, NULL, __dso_handle);
}

void __cxa_finalize(void *dso) {
    for (;;) {
        struct cxa_exit_record selected = {0};
        size_t selected_index = AKAOS_CXA_EXIT_MAX;

        exit_table_acquire();
        for (size_t index = 0; index < AKAOS_CXA_EXIT_MAX; ++index) {
            struct cxa_exit_record *record = &exit_records[index];
            if (!record->active || (dso && record->dso != dso) ||
                record->order <= selected.order)
                continue;
            selected = *record;
            selected_index = index;
        }
        if (selected_index < AKAOS_CXA_EXIT_MAX)
            exit_records[selected_index].active = 0;
        exit_table_release();

        if (selected_index == AKAOS_CXA_EXIT_MAX) return;
        selected.destructor(selected.argument);
    }
}

static pthread_once_t guard_mutex_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t guard_mutex;
static int guard_mutex_ready;

static void initialize_guard_mutex(void) {
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes) != 0) return;
    if (pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) == 0 &&
        pthread_mutex_init(&guard_mutex, &attributes) == 0)
        guard_mutex_ready = 1;
    (void)pthread_mutexattr_destroy(&attributes);
}

static void acquire_guard_mutex(void) {
    if (pthread_once(&guard_mutex_once, initialize_guard_mutex) != 0 ||
        !guard_mutex_ready || pthread_mutex_lock(&guard_mutex) != 0)
        abort();
}

static void release_guard_mutex(void) {
    if (pthread_mutex_unlock(&guard_mutex) != 0) abort();
}

int __cxa_guard_acquire(uint64_t *guard) {
    if (!guard) abort();
    acquire_guard_mutex();
    const volatile unsigned char *state = (const volatile unsigned char *)guard;
    if ((__atomic_load_n(state, __ATOMIC_ACQUIRE) & 1U) != 0U) {
        release_guard_mutex();
        return 0;
    }
    return 1;
}

void __cxa_guard_release(uint64_t *guard) {
    if (!guard) abort();
    volatile unsigned char *state = (volatile unsigned char *)guard;
    __atomic_store_n(state, 1U, __ATOMIC_RELEASE);
    release_guard_mutex();
}

void __cxa_guard_abort(uint64_t *guard) {
    if (!guard) abort();
    release_guard_mutex();
}

void __cxa_pure_virtual(void) {
    abort();
}

/* Keep the public std::nothrow object available to the freestanding C++
 * header. The allocation/deallocation operators themselves come from the
 * pinned upstream libc++abi runtime, which implements new_handler and throws
 * std::bad_alloc on allocation failure. */
struct akaos_nothrow_tag {
    unsigned char value;
};

const struct akaos_nothrow_tag akaos_nothrow_value
    __asm__("_ZSt7nothrow") = {0U};
