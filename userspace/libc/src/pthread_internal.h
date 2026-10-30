#ifndef AKAOS_LIBC_PTHREAD_INTERNAL_H
#define AKAOS_LIBC_PTHREAD_INTERNAL_H

#include <pthread.h>
#include <stdint.h>

#define AKAOS_PTHREAD_ATFORK_MAX 32U

struct akaos_pthread_atfork_handler {
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
};

struct akaos_pthread_atfork_snapshot {
    uint32_t count;
    struct akaos_pthread_atfork_handler
        handlers[AKAOS_PTHREAD_ATFORK_MAX];
};

void __akaos_pthread_atfork_snapshot(
    struct akaos_pthread_atfork_snapshot *snapshot);
void __akaos_pthread_atfork_prepare(
    const struct akaos_pthread_atfork_snapshot *snapshot);
void __akaos_pthread_atfork_fork_lock(void);
void __akaos_pthread_atfork_parent(
    const struct akaos_pthread_atfork_snapshot *snapshot);
void __akaos_pthread_atfork_child(
    const struct akaos_pthread_atfork_snapshot *snapshot,
    pthread_t caller_thread);
void akaos_pthread_set_initial_stack(uintptr_t initial_stack_pointer);

#endif
