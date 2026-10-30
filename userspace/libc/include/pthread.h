#ifndef AKAOS_LIBC_PTHREAD_H
#define AKAOS_LIBC_PTHREAD_H

/*
 * Portable pthread ABI for akaOS.
 *
 * The objects deliberately contain no pointers or host libc state, so they
 * have the same layout on x86, ARM and RISC-V and can be placed in shared
 * memory.  pthread_create uses XSCore's native shared-address-space thread
 * ABI; mutexes, condition variables, once, identity and yielding are all
 * implemented without host-libc state.
 */

#include <stdint.h>
#include <stddef.h>
#include <signal.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pthread_t;
typedef uint32_t pthread_key_t;
typedef struct {
    volatile uint32_t state;
    pthread_t owner;
    uint32_t recursion;
    int type;
    int pshared;
} pthread_mutex_t;
typedef struct {
    volatile uint32_t sequence;
    int pshared;
    int clock_id;
} pthread_cond_t;
typedef struct { volatile uint32_t state; } pthread_once_t;
typedef struct {
    volatile uint32_t state;
    uint32_t trip_count;
    volatile uint32_t generation;
    int pshared;
} pthread_barrier_t;
typedef struct {
    uint32_t flags;
    size_t stack_size;
    int detach_state;
} pthread_attr_t;
typedef struct { int type; int pshared; } pthread_mutexattr_t;
typedef struct { int pshared; int clock_id; } pthread_condattr_t;
typedef struct { int pshared; } pthread_rwlockattr_t;
typedef struct { int pshared; } pthread_barrierattr_t;
typedef struct {
    volatile uint32_t state;
    int pshared;
} pthread_rwlock_t;

#define PTHREAD_MUTEX_INITIALIZER { 0U }
#define PTHREAD_COND_INITIALIZER  { 0U }
#define PTHREAD_ONCE_INIT         { 0U }
#define PTHREAD_RWLOCK_INITIALIZER { 0U }
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

#define PTHREAD_CREATE_JOINABLE 0U
#define PTHREAD_CREATE_DETACHED 1U
#define PTHREAD_KEYS_MAX 32U
#define PTHREAD_DESTRUCTOR_ITERATIONS 4U
#define PTHREAD_STACK_MIN 16384U
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void *)(intptr_t)-1)

int pthread_attr_init(pthread_attr_t *attributes);
int pthread_attr_destroy(pthread_attr_t *attributes);
int pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t *attributes, size_t *size);
int pthread_attr_getstack(const pthread_attr_t *attributes,
                          void **stack_address, size_t *stack_size);
int pthread_attr_setdetachstate(pthread_attr_t *attributes, int state);
int pthread_attr_getdetachstate(const pthread_attr_t *attributes, int *state);
int pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes);

int pthread_mutexattr_init(pthread_mutexattr_t *attributes);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attributes);
int pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attributes, int *type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attributes, int pshared);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attributes,
                                 int *pshared);

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attributes);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_cond_init(pthread_cond_t *condition, const void *attributes);
int pthread_cond_destroy(pthread_cond_t *condition);
int pthread_condattr_init(pthread_condattr_t *attributes);
int pthread_condattr_destroy(pthread_condattr_t *attributes);
int pthread_condattr_setpshared(pthread_condattr_t *attributes, int pshared);
int pthread_condattr_getpshared(const pthread_condattr_t *attributes,
                                int *pshared);
int pthread_condattr_setclock(pthread_condattr_t *attributes, int clock_id);
int pthread_condattr_getclock(const pthread_condattr_t *attributes,
                              int *clock_id);
int pthread_cond_signal(pthread_cond_t *condition);
int pthread_cond_broadcast(pthread_cond_t *condition);
int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *abstime);

int pthread_rwlock_init(pthread_rwlock_t *lock, const void *attributes);
int pthread_rwlock_destroy(pthread_rwlock_t *lock);
int pthread_rwlockattr_init(pthread_rwlockattr_t *attributes);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attributes);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attributes, int pshared);
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attributes,
                                  int *pshared);
int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock);
int pthread_rwlock_wrlock(pthread_rwlock_t *lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock);
int pthread_rwlock_unlock(pthread_rwlock_t *lock);

int pthread_once(pthread_once_t *once, void (*initializer)(void));
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old_set);
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const void *attributes, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);
int pthread_barrierattr_init(pthread_barrierattr_t *attributes);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attributes);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *attributes,
                                   int pshared);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attributes,
                                   int *pshared);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
void *pthread_getspecific(pthread_key_t key);
int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*start_routine)(void *), void *argument);
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void));
int pthread_join(pthread_t thread, void **value);
int pthread_detach(pthread_t thread);
__attribute__((noreturn)) void pthread_exit(void *value);
int pthread_cancel(pthread_t thread);
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);

/* POSIX cleanup handlers are represented by a small caller-owned stack.  The
 * macros preserve the standard lexical push/pop shape while the implementation
 * keeps handler storage in the shared akaOS pthread runtime. */
void akaos_pthread_cleanup_push(void (*routine)(void *), void *argument);
void akaos_pthread_cleanup_pop(int execute);
#define pthread_cleanup_push(routine, argument) \
    do { \
        akaos_pthread_cleanup_push((routine), (argument)); \
        {
#define pthread_cleanup_pop(execute) \
        } \
        akaos_pthread_cleanup_pop((execute)); \
    } while (0)

/* akaOS native thread-pointer hooks.  These are intentionally public to
 * libc/loader clients so static TLS can install the ABI TP for each thread. */
int akaos_thread_pointer_set(uintptr_t value);
uintptr_t akaos_thread_pointer_get(void);
void pthread_testcancel(void);

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif
