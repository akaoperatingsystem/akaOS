#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Some 32-bit ABIs lower 64-bit C/C++ atomics to these libatomic entry points.
 * akaOS implements them with one process-local mutex, giving every operation
 * stronger-than-requested ordering while keeping the 64-bit data access
 * aligned and indivisible on RV32/ARM32. */
static pthread_mutex_t atomic64_mutex = PTHREAD_MUTEX_INITIALIZER;

enum atomic64_operation {
    ATOMIC64_ADD,
    ATOMIC64_SUB,
    ATOMIC64_AND,
    ATOMIC64_OR,
    ATOMIC64_XOR,
    ATOMIC64_NAND
};

static uint64_t atomic64_load_locked(const volatile void *object) {
    uint64_t value = 0U;
    memcpy(&value, (const void *)object, sizeof(value));
    return value;
}

static void atomic64_store_locked(volatile void *object, uint64_t value) {
    memcpy((void *)object, &value, sizeof(value));
}

uint64_t __atomic_load_8(const volatile void *object, int memory_order) {
    (void)memory_order;
    (void)pthread_mutex_lock(&atomic64_mutex);
    uint64_t value = atomic64_load_locked(object);
    (void)pthread_mutex_unlock(&atomic64_mutex);
    return value;
}

void __atomic_store_8(volatile void *object, uint64_t value,
                      int memory_order) {
    (void)memory_order;
    (void)pthread_mutex_lock(&atomic64_mutex);
    atomic64_store_locked(object, value);
    (void)pthread_mutex_unlock(&atomic64_mutex);
}

uint64_t __atomic_exchange_8(volatile void *object, uint64_t value,
                             int memory_order) {
    (void)memory_order;
    (void)pthread_mutex_lock(&atomic64_mutex);
    uint64_t previous = atomic64_load_locked(object);
    atomic64_store_locked(object, value);
    (void)pthread_mutex_unlock(&atomic64_mutex);
    return previous;
}

bool __atomic_compare_exchange_8(volatile void *object, void *expected,
                                 uint64_t desired, bool weak,
                                 int success_order, int failure_order) {
    (void)weak;
    (void)success_order;
    (void)failure_order;
    if (!expected) return false;
    uint64_t wanted = 0U;
    memcpy(&wanted, expected, sizeof(wanted));
    (void)pthread_mutex_lock(&atomic64_mutex);
    uint64_t observed = atomic64_load_locked(object);
    bool exchanged = observed == wanted;
    if (exchanged) atomic64_store_locked(object, desired);
    else memcpy(expected, &observed, sizeof(observed));
    (void)pthread_mutex_unlock(&atomic64_mutex);
    return exchanged;
}

static uint64_t atomic64_fetch(volatile void *object, uint64_t value,
                               enum atomic64_operation operation,
                               int return_new, int memory_order) {
    (void)memory_order;
    (void)pthread_mutex_lock(&atomic64_mutex);
    uint64_t previous = atomic64_load_locked(object);
    uint64_t updated;
    switch (operation) {
        case ATOMIC64_ADD: updated = previous + value; break;
        case ATOMIC64_SUB: updated = previous - value; break;
        case ATOMIC64_AND: updated = previous & value; break;
        case ATOMIC64_OR: updated = previous | value; break;
        case ATOMIC64_XOR: updated = previous ^ value; break;
        case ATOMIC64_NAND: updated = ~(previous & value); break;
        default: updated = previous; break;
    }
    atomic64_store_locked(object, updated);
    (void)pthread_mutex_unlock(&atomic64_mutex);
    return return_new ? updated : previous;
}

uint64_t __atomic_fetch_add_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_ADD, 0, memory_order);
}

uint64_t __atomic_add_fetch_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_ADD, 1, memory_order);
}

uint64_t __atomic_fetch_sub_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_SUB, 0, memory_order);
}

uint64_t __atomic_sub_fetch_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_SUB, 1, memory_order);
}

uint64_t __atomic_fetch_and_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_AND, 0, memory_order);
}

uint64_t __atomic_and_fetch_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_AND, 1, memory_order);
}

uint64_t __atomic_fetch_or_8(volatile void *object, uint64_t value,
                             int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_OR, 0, memory_order);
}

uint64_t __atomic_or_fetch_8(volatile void *object, uint64_t value,
                             int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_OR, 1, memory_order);
}

uint64_t __atomic_fetch_xor_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_XOR, 0, memory_order);
}

uint64_t __atomic_xor_fetch_8(volatile void *object, uint64_t value,
                              int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_XOR, 1, memory_order);
}

uint64_t __atomic_fetch_nand_8(volatile void *object, uint64_t value,
                               int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_NAND, 0, memory_order);
}

uint64_t __atomic_nand_fetch_8(volatile void *object, uint64_t value,
                               int memory_order) {
    return atomic64_fetch(object, value, ATOMIC64_NAND, 1, memory_order);
}
