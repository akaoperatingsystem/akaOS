#include <errno.h>
#include <akaos/syscall.h>
#include <akaos/futex.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "pthread_internal.h"

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old_set) {
    if (sigprocmask(how, set, old_set) == 0) return 0;
    return errno;
}

int akaos_sched_yield(void);
long akaos_thread_syscall(uint64_t number, uint64_t argument0,
                          uint64_t argument1, uint64_t argument2);

int akaos_thread_pointer_set(uintptr_t value) {
    long result = akaos_thread_syscall(AKAOS_SYSCALL_SET_TP,
                                       (uint64_t)value, 0, 0);
    if (result < 0) return (int)-result;
#if defined(__aarch64__)
    __asm__ volatile("msr tpidr_el0, %0" :: "r"((uint64_t)value) : "memory");
    __asm__ volatile("isb" ::: "memory");
#elif defined(__arm__)
    /* ARM32 keeps the authoritative TP in XSCore's transition record.  The
     * user-mode MCR form is not permitted for TPIDRURO on all Cortex-A7
     * configurations, so only the privileged return path mirrors it into the
     * architectural register when needed. */
#elif defined(__riscv)
    __asm__ volatile("mv tp, %0" :: "r"((uintptr_t)value) : "memory");
#endif
    return 0;
}

uintptr_t akaos_thread_pointer_get(void) {
    long result = akaos_thread_syscall(AKAOS_SYSCALL_GET_TP, 0, 0, 0);
    return result < 0 ? 0 : (uintptr_t)result;
}

struct pthread_start_context {
    void *(*start_routine)(void *);
    void *argument;
};

#define AKAOS_PTHREAD_RECORDS 64U
struct pthread_record {
    pthread_t id;
    void *mapping;
    size_t length;
    uint32_t used;
    uint32_t detached;
    volatile uint32_t cancel_requested;
    int cancel_state;
    int cancel_type;
};

#define AKAOS_PTHREAD_CLEANUP_MAX 16U
struct pthread_cleanup_entry {
    void (*routine)(void *);
    void *argument;
};

/* Keep the desktop-capable x86-64 default at 1 MiB. The remaining ports run
 * with smaller default guest memory/address windows, so leave room for several
 * C++ worker stacks without making ordinary thread creation fail. */
#if defined(__x86_64__)
#define AKAOS_PTHREAD_STACK_DEFAULT (1024U * 1024U)
#else
#define AKAOS_PTHREAD_STACK_DEFAULT (256U * 1024U)
#endif
#define AKAOS_PTHREAD_STACK_MINIMUM (16U * 1024U)
#define AKAOS_PTHREAD_STACK_MAXIMUM (8U * 1024U * 1024U)
#define AKAOS_INITIAL_STACK_PAGE_SIZE 4096U
#define AKAOS_INITIAL_STACK_MAX_PAGES 64U
#define AKAOS_ATTR_STACK_RECORDS 64U

struct pthread_attr_stack_record {
    const pthread_attr_t *attributes;
    void *address;
    size_t size;
};

#define AKAOS_TLS_THREAD_SLOTS 64U
struct pthread_tls_key {
    uint32_t used;
    void (*destructor)(void *);
};
struct pthread_tls_value {
    pthread_t thread;
    void *value;
};
static struct pthread_tls_key pthread_tls_keys[PTHREAD_KEYS_MAX];
static struct pthread_tls_value pthread_tls_values[PTHREAD_KEYS_MAX]
                                                    [AKAOS_TLS_THREAD_SLOTS];
static struct pthread_record pthread_records[AKAOS_PTHREAD_RECORDS];
static struct pthread_cleanup_entry pthread_cleanup_entries
    [AKAOS_PTHREAD_RECORDS][AKAOS_PTHREAD_CLEANUP_MAX];
static uint32_t pthread_cleanup_depth[AKAOS_PTHREAD_RECORDS];
static struct pthread_cleanup_entry pthread_main_cleanup_entries
    [AKAOS_PTHREAD_CLEANUP_MAX];
static uint32_t pthread_main_cleanup_depth;
static volatile uint32_t pthread_records_lock;
static struct pthread_attr_stack_record
    pthread_attr_stack_records[AKAOS_ATTR_STACK_RECORDS];
static uintptr_t pthread_initial_stack_top;
struct pthread_errno_slot {
    pthread_t thread;
    int value;
    int resolver_value;
};
static struct pthread_errno_slot pthread_errno_slots[AKAOS_PTHREAD_RECORDS];
static int pthread_main_errno;
static int pthread_main_h_errno;
static volatile uint32_t pthread_errno_lock;
static struct akaos_pthread_atfork_handler
    pthread_atfork_handlers[AKAOS_PTHREAD_ATFORK_MAX];
static uint32_t pthread_atfork_handler_count;
/* The initial thread has no pthread_record allocation. Keep its cancellation
 * state in process-local storage so pthread_self()/pthread_cancel() retain
 * the expected semantics for the shell and resident services. */
static volatile uint32_t pthread_main_cancel_requested;
static int pthread_main_cancel_state = PTHREAD_CANCEL_ENABLE;
static int pthread_main_cancel_type = PTHREAD_CANCEL_DEFERRED;
static volatile pthread_t pthread_initial_thread;

struct akaos_tls_index {
    uintptr_t module;
    uintptr_t offset;
};

#if defined(__i386__)
/* i386's general-dynamic TLS ABI passes the descriptor in EAX rather than
 * using the normal C stack convention.  Clang's regparm(1) attribute models
 * that ABI exactly while leaving the implementation portable C. */
#define AKAOS_TLS_ENTRY __attribute__((regparm(1)))
#else
#define AKAOS_TLS_ENTRY
#endif

/* ELF general-dynamic TLS entry point.  The loader owns module templates;
 * this libc shim supplies the current akaOS thread's block address. */
void *AKAOS_TLS_ENTRY __tls_get_addr(const struct akaos_tls_index *index) {
    if (!index) return NULL;
    uintptr_t offset = index->offset;
#if defined(__riscv)
    /* RISC-V encodes DTPREL with the ABI's 0x800 TLS_DTV_OFFSET bias.
     * Remove it before passing the module-relative byte offset to the loader. */
    offset += (uintptr_t)0x800U;
#endif
    return akaos_dl_tls_address(index->module, offset);
}

/* The i386 ELF TLS ABI spells the resolver symbol with one additional
 * leading underscore (___tls_get_addr).  Keep the canonical libc entry above
 * and expose the ABI spelling as a tiny forwarding alias so 32-bit PIC DSOs
 * use exactly the same implementation as every other port. */
#if defined(__i386__)
void *AKAOS_TLS_ENTRY ___tls_get_addr(const struct akaos_tls_index *index) {
    return __tls_get_addr(index);
}
#endif

#if defined(__arm__)
/* ARM EABI static-TLS code obtains the current TP through this ABI helper.
 * Reading through the syscall preserves the normal callee-saved r9 used by
 * position-independent application code while retaining per-thread state. */
__attribute__((naked)) void *__aeabi_read_tp(void) {
    /* The ARM initial-exec sequence keeps the TPOFF value in r1 across this
     * call, even though AAPCS normally treats r1 as caller-saved.  Mirror the
     * Linux helper's tiny register footprint: issue GET_TP directly and
     * preserve r1-r3/r12 while returning the kernel result in r0. */
    __asm__ volatile(
        "push {r1-r3, r12, lr}\n"
        "mov r7, #70\n"
        "mov r0, #0\n"
        "mov r1, #0\n"
        "mov r2, #0\n"
        "svc #0\n"
        "pop {r1-r3, r12, pc}\n");
}
#endif

static void pthread_records_acquire(void) {
    uint32_t expected;
    do {
        expected = 0;
    } while (!__atomic_compare_exchange_n(&pthread_records_lock, &expected, 1U,
                                          0, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
}

static void pthread_records_release(void) {
    __atomic_store_n(&pthread_records_lock, 0U, __ATOMIC_RELEASE);
}

static void pthread_attr_stack_forget(const pthread_attr_t *attributes) {
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_ATTR_STACK_RECORDS; ++i) {
        if (pthread_attr_stack_records[i].attributes != attributes) continue;
        memset(&pthread_attr_stack_records[i], 0,
               sizeof(pthread_attr_stack_records[i]));
        break;
    }
    pthread_records_release();
}

static int pthread_attr_stack_remember(const pthread_attr_t *attributes,
                                       void *address, size_t size) {
    pthread_records_acquire();
    uint32_t available = AKAOS_ATTR_STACK_RECORDS;
    for (uint32_t i = 0; i < AKAOS_ATTR_STACK_RECORDS; ++i) {
        if (pthread_attr_stack_records[i].attributes == attributes) {
            available = i;
            break;
        }
        if (available == AKAOS_ATTR_STACK_RECORDS &&
            !pthread_attr_stack_records[i].attributes)
            available = i;
    }
    if (available == AKAOS_ATTR_STACK_RECORDS) {
        pthread_records_release();
        return EAGAIN;
    }
    pthread_attr_stack_records[available].attributes = attributes;
    pthread_attr_stack_records[available].address = address;
    pthread_attr_stack_records[available].size = size;
    pthread_records_release();
    return 0;
}

void akaos_pthread_set_initial_stack(uintptr_t initial_stack_pointer) {
    if (!initial_stack_pointer) return;
    uintptr_t page_mask = (uintptr_t)AKAOS_INITIAL_STACK_PAGE_SIZE - 1U;
    pthread_initial_stack_top =
        (initial_stack_pointer + page_mask) & ~page_mask;
    (void)pthread_self();
}

static void pthread_errno_acquire(void) {
    uint32_t expected;
    do {
        expected = 0U;
    } while (!__atomic_compare_exchange_n(&pthread_errno_lock, &expected, 1U,
                                          0, __ATOMIC_ACQUIRE,
                                          __ATOMIC_RELAXED));
}

static void pthread_errno_release(void) {
    __atomic_store_n(&pthread_errno_lock, 0U, __ATOMIC_RELEASE);
}

int *__errno_location(void) {
    long result = akaos_thread_syscall(AKAOS_SYSCALL_GETTID, 0, 0, 0);
    if (result <= 0) return &pthread_main_errno;
    pthread_t thread = (pthread_t)result;

    pthread_errno_acquire();
    struct pthread_errno_slot *free_slot = NULL;
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        struct pthread_errno_slot *slot = &pthread_errno_slots[index];
        if (slot->thread == thread) {
            pthread_errno_release();
            return &slot->value;
        }
        if (!slot->thread && !free_slot) free_slot = slot;
    }
    if (free_slot) {
        free_slot->value = 0;
        free_slot->resolver_value = 0;
        free_slot->thread = thread;
        pthread_errno_release();
        return &free_slot->value;
    }
    pthread_errno_release();
    /* The table matches pthread's bounded live-thread capacity. Preserve a
     * usable errno reference if an internal, non-pthread task exceeds it. */
    return &pthread_main_errno;
}

int *__h_errno_location(void) {
    long result = akaos_thread_syscall(AKAOS_SYSCALL_GETTID, 0, 0, 0);
    if (result <= 0) return &pthread_main_h_errno;
    pthread_t thread = (pthread_t)result;

    pthread_errno_acquire();
    struct pthread_errno_slot *free_slot = NULL;
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        struct pthread_errno_slot *slot = &pthread_errno_slots[index];
        if (slot->thread == thread) {
            pthread_errno_release();
            return &slot->resolver_value;
        }
        if (!slot->thread && !free_slot) free_slot = slot;
    }
    if (free_slot) {
        free_slot->value = 0;
        free_slot->resolver_value = 0;
        free_slot->thread = thread;
        pthread_errno_release();
        return &free_slot->resolver_value;
    }
    pthread_errno_release();
    return &pthread_main_h_errno;
}

static void pthread_errno_release_thread(pthread_t thread) {
    if (!thread) return;
    pthread_errno_acquire();
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        struct pthread_errno_slot *slot = &pthread_errno_slots[index];
        if (slot->thread != thread) continue;
        slot->value = 0;
        slot->resolver_value = 0;
        slot->thread = 0U;
        break;
    }
    pthread_errno_release();
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void)) {
    pthread_records_acquire();
    if (pthread_atfork_handler_count >= AKAOS_PTHREAD_ATFORK_MAX) {
        pthread_records_release();
        return ENOMEM;
    }
    struct akaos_pthread_atfork_handler *handler =
        &pthread_atfork_handlers[pthread_atfork_handler_count++];
    handler->prepare = prepare;
    handler->parent = parent;
    handler->child = child;
    pthread_records_release();
    return 0;
}

void __akaos_pthread_atfork_snapshot(
    struct akaos_pthread_atfork_snapshot *snapshot) {
    if (!snapshot) return;
    pthread_records_acquire();
    uint32_t count = pthread_atfork_handler_count;
    if (count > AKAOS_PTHREAD_ATFORK_MAX)
        count = AKAOS_PTHREAD_ATFORK_MAX;
    snapshot->count = count;
    for (uint32_t index = 0; index < count; ++index)
        snapshot->handlers[index] = pthread_atfork_handlers[index];
    pthread_records_release();
}

void __akaos_pthread_atfork_prepare(
    const struct akaos_pthread_atfork_snapshot *snapshot) {
    if (!snapshot) return;
    uint32_t count = snapshot->count;
    if (count > AKAOS_PTHREAD_ATFORK_MAX)
        count = AKAOS_PTHREAD_ATFORK_MAX;
    while (count != 0U) {
        void (*handler)(void) = snapshot->handlers[--count].prepare;
        #if __SIZEOF_POINTER__ == 8
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < UINT64_C(0x0000800000000000)) handler();
#else
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < 0xc0000000U) handler();
#endif
    }
}

void __akaos_pthread_atfork_fork_lock(void) {
    /* Freeze pthread/TSD records across the kernel's address-space snapshot.
     * Parent releases this before callbacks; child repairs and resets it. */
    pthread_records_acquire();
}

void __akaos_pthread_atfork_parent(
    const struct akaos_pthread_atfork_snapshot *snapshot) {
    pthread_records_release();
    if (!snapshot) return;
    uint32_t count = snapshot->count;
    if (count > AKAOS_PTHREAD_ATFORK_MAX)
        count = AKAOS_PTHREAD_ATFORK_MAX;
    for (uint32_t index = 0; index < count; ++index) {
        void (*handler)(void) = snapshot->handlers[index].parent;
        #if __SIZEOF_POINTER__ == 8
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < UINT64_C(0x0000800000000000)) handler();
#else
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < 0xc0000000U) handler();
#endif
    }
}

void __akaos_pthread_atfork_child(
    const struct akaos_pthread_atfork_snapshot *snapshot,
    pthread_t caller_thread) {
    void *abandoned_mappings[AKAOS_PTHREAD_RECORDS];
    size_t abandoned_lengths[AKAOS_PTHREAD_RECORDS];
    uint32_t abandoned_count = 0U;
    pthread_t child_thread = (pthread_t)akaos_thread_syscall(
        AKAOS_SYSCALL_GETTID, 0, 0, 0);
    if (!child_thread) child_thread = caller_thread;

    /* Only the calling thread survives fork. A sibling may have held this
     * process-local lock at the snapshot, so reset it before preserving the
     * caller's errno and discarding vanished threads' slots. */
    __atomic_store_n(&pthread_errno_lock, 0U, __ATOMIC_RELEASE);
    pthread_errno_acquire();
    int caller_errno_found = 0;
    struct pthread_errno_slot *free_errno_slot = NULL;
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        struct pthread_errno_slot *slot = &pthread_errno_slots[index];
        if (caller_thread && slot->thread == caller_thread) {
            slot->thread = child_thread;
            caller_errno_found = 1;
        } else if (slot->thread) {
            slot->thread = 0U;
            slot->value = 0;
            slot->resolver_value = 0;
        }
        if (!slot->thread && !free_errno_slot) free_errno_slot = slot;
    }
    if (!caller_errno_found && free_errno_slot) {
        free_errno_slot->thread = child_thread;
        free_errno_slot->value = 0;
        free_errno_slot->resolver_value = 0;
    }
    pthread_errno_release();

    int caller_record = -1;
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        struct pthread_record *record = &pthread_records[index];
        if (!record->used) continue;
        if (record->id == caller_thread) {
            record->id = child_thread;
            caller_record = (int)index;
            continue;
        }
        if (record->mapping && abandoned_count < AKAOS_PTHREAD_RECORDS) {
            abandoned_mappings[abandoned_count] = record->mapping;
            abandoned_lengths[abandoned_count] = record->length;
            ++abandoned_count;
        }
        memset(record, 0, sizeof(*record));
        pthread_cleanup_depth[index] = 0U;
        memset(pthread_cleanup_entries[index], 0,
               sizeof(pthread_cleanup_entries[index]));
    }

    for (uint32_t key = 0; key < PTHREAD_KEYS_MAX; ++key) {
        for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
            struct pthread_tls_value *value = &pthread_tls_values[key][slot];
            if (value->thread == caller_thread) {
                value->thread = child_thread;
            } else if (value->thread != 0U) {
                value->thread = 0U;
                value->value = NULL;
            }
        }
    }

    if (caller_record >= 0) {
        pthread_main_cleanup_depth = 0U;
        memset(pthread_main_cleanup_entries, 0,
               sizeof(pthread_main_cleanup_entries));
        __atomic_store_n(&pthread_main_cancel_requested, 0U,
                         __ATOMIC_RELEASE);
        pthread_main_cancel_state = PTHREAD_CANCEL_ENABLE;
        pthread_main_cancel_type = PTHREAD_CANCEL_DEFERRED;
    }
    __atomic_store_n(&pthread_initial_thread, child_thread, __ATOMIC_RELEASE);
    /* The parent entered fork holding the records lock. No vanished thread
     * can own it now, so the child must reset it before unmapping stacks or
     * invoking application child handlers. */
    pthread_records_release();

    for (uint32_t index = 0; index < abandoned_count; ++index)
        (void)munmap(abandoned_mappings[index], abandoned_lengths[index]);

    if (!snapshot) return;
    uint32_t count = snapshot->count;
    if (count > AKAOS_PTHREAD_ATFORK_MAX)
        count = AKAOS_PTHREAD_ATFORK_MAX;
    for (uint32_t index = 0; index < count; ++index) {
        void (*handler)(void) = snapshot->handlers[index].child;
        #if __SIZEOF_POINTER__ == 8
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < UINT64_C(0x0000800000000000)) handler();
#else
        if (handler && (uintptr_t)handler >= 0x1000U && (uintptr_t)handler < 0xc0000000U) handler();
#endif
    }
}

static int pthread_cleanup_record_index_locked(pthread_t thread) {
    if (!thread) return -1;
    for (uint32_t index = 0; index < AKAOS_PTHREAD_RECORDS; ++index) {
        if (pthread_records[index].used &&
            pthread_records[index].id == thread)
            return (int)index;
    }
    return -1;
}

void akaos_pthread_cleanup_push(void (*routine)(void *), void *argument) {
    if (!routine) return;
    pthread_t thread = pthread_self();
    pthread_t initial = __atomic_load_n(&pthread_initial_thread,
                                        __ATOMIC_ACQUIRE);
    pthread_records_acquire();
    int record = pthread_cleanup_record_index_locked(thread);
    if (record < 0 && (!thread || (initial != 0U && thread == initial))) {
        if (pthread_main_cleanup_depth < AKAOS_PTHREAD_CLEANUP_MAX) {
            uint32_t depth = pthread_main_cleanup_depth++;
            pthread_main_cleanup_entries[depth].routine = routine;
            pthread_main_cleanup_entries[depth].argument = argument;
        }
        pthread_records_release();
        return;
    }
    if (record >= 0 &&
        pthread_cleanup_depth[(uint32_t)record] < AKAOS_PTHREAD_CLEANUP_MAX) {
        uint32_t depth = pthread_cleanup_depth[(uint32_t)record]++;
        pthread_cleanup_entries[(uint32_t)record][depth].routine = routine;
        pthread_cleanup_entries[(uint32_t)record][depth].argument = argument;
    }
    pthread_records_release();
}

void akaos_pthread_cleanup_pop(int execute) {
    pthread_t thread = pthread_self();
    pthread_t initial = __atomic_load_n(&pthread_initial_thread,
                                        __ATOMIC_ACQUIRE);
    struct pthread_cleanup_entry entry = {0, 0};
    int found = 0;
    pthread_records_acquire();
    int record = pthread_cleanup_record_index_locked(thread);
    if (record < 0 && (!thread || (initial != 0U && thread == initial))) {
        if (pthread_main_cleanup_depth != 0U) {
            uint32_t depth = --pthread_main_cleanup_depth;
            entry = pthread_main_cleanup_entries[depth];
            pthread_main_cleanup_entries[depth].routine = 0;
            pthread_main_cleanup_entries[depth].argument = 0;
            found = 1;
        }
    } else {
        if (record >= 0 && pthread_cleanup_depth[(uint32_t)record] != 0U) {
            uint32_t depth = --pthread_cleanup_depth[(uint32_t)record];
            entry = pthread_cleanup_entries[(uint32_t)record][depth];
            pthread_cleanup_entries[(uint32_t)record][depth].routine = 0;
            pthread_cleanup_entries[(uint32_t)record][depth].argument = 0;
            found = 1;
        }
    }
    pthread_records_release();
    if (execute && found && entry.routine) entry.routine(entry.argument);
}

static void pthread_run_cleanup_handlers(void) {
    pthread_t thread = pthread_self();
    pthread_t initial = __atomic_load_n(&pthread_initial_thread,
                                        __ATOMIC_ACQUIRE);
    for (;;) {
        struct pthread_cleanup_entry entry = {0, 0};
        int found = 0;
        pthread_records_acquire();
        int record = pthread_cleanup_record_index_locked(thread);
        if (record < 0 && (!thread || (initial != 0U && thread == initial))) {
            if (pthread_main_cleanup_depth != 0U) {
                uint32_t depth = --pthread_main_cleanup_depth;
                entry = pthread_main_cleanup_entries[depth];
                pthread_main_cleanup_entries[depth].routine = 0;
                pthread_main_cleanup_entries[depth].argument = 0;
                found = 1;
            }
        } else {
            if (record >= 0 &&
                pthread_cleanup_depth[(uint32_t)record] != 0U) {
                uint32_t depth = --pthread_cleanup_depth[(uint32_t)record];
                entry = pthread_cleanup_entries[(uint32_t)record][depth];
                pthread_cleanup_entries[(uint32_t)record][depth].routine = 0;
                pthread_cleanup_entries[(uint32_t)record][depth].argument = 0;
                found = 1;
            }
        }
        pthread_records_release();
        if (!found) break;
        if (entry.routine) entry.routine(entry.argument);
    }
}

/* Detached threads cannot be joined, so reclaim their userspace stack
 * mapping once the kernel reports that execution has ended.  The kernel
 * stack/TCB is reclaimed independently by XSCore's scheduler sweep. */
static void pthread_reap_detached_records(void) {
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        pthread_t thread = 0;
        void *mapping = 0;
        size_t length = 0;
        pthread_records_acquire();
        if (pthread_records[i].used && pthread_records[i].detached) {
            thread = pthread_records[i].id;
            mapping = pthread_records[i].mapping;
            length = pthread_records[i].length;
        }
        pthread_records_release();
        if (!thread) continue;

        long state = akaos_thread_syscall(AKAOS_SYSCALL_THREAD_STATE,
                                          (uint64_t)thread, 0, 0);
        /* EXITED and REAPING no longer execute on the userspace stack; 0
         * means the kernel has already completed automatic TCB reaping. */
        if (state != AKAOS_THREAD_STATE_UNUSED &&
            state != AKAOS_THREAD_STATE_EXITED &&
            state != AKAOS_THREAD_STATE_REAPING) continue;

        int reclaimed = 0;
        pthread_records_acquire();
        if (pthread_records[i].used && pthread_records[i].detached &&
            pthread_records[i].id == thread) {
            pthread_records[i].used = 0;
            pthread_records[i].detached = 0;
            pthread_cleanup_depth[i] = 0U;
            memset(pthread_cleanup_entries[i], 0,
                   sizeof(pthread_cleanup_entries[i]));
            reclaimed = 1;
        }
        pthread_records_release();
        if (reclaimed) (void)munmap(mapping, length);
    }
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;
    pthread_records_acquire();
    for (pthread_key_t i = 0; i < PTHREAD_KEYS_MAX; ++i) {
        if (pthread_tls_keys[i].used) continue;
        pthread_tls_keys[i].used = 1;
        pthread_tls_keys[i].destructor = destructor;
        for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
            pthread_tls_values[i][slot].thread = 0;
            pthread_tls_values[i][slot].value = 0;
        }
        *key = i;
        pthread_records_release();
        return 0;
    }
    pthread_records_release();
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    pthread_records_acquire();
    if (!pthread_tls_keys[key].used) {
        pthread_records_release();
        return EINVAL;
    }
    pthread_tls_keys[key].used = 0;
    pthread_tls_keys[key].destructor = 0;
    for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        pthread_tls_values[key][slot].thread = 0;
        pthread_tls_values[key][slot].value = 0;
    }
    pthread_records_release();
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    pthread_t thread = pthread_self();
    if (!thread) return ESRCH;
    pthread_records_acquire();
    if (!pthread_tls_keys[key].used) {
        pthread_records_release();
        return EINVAL;
    }
    uint32_t free_slot = AKAOS_TLS_THREAD_SLOTS;
    for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        if (pthread_tls_values[key][slot].thread == thread) {
            if (value) {
                pthread_tls_values[key][slot].value = (void *)value;
            } else {
                pthread_tls_values[key][slot].thread = 0;
                pthread_tls_values[key][slot].value = 0;
            }
            pthread_records_release();
            return 0;
        }
        if (!pthread_tls_values[key][slot].thread && free_slot == AKAOS_TLS_THREAD_SLOTS)
            free_slot = slot;
    }
    if (!value) {
        pthread_records_release();
        return 0;
    }
    if (free_slot == AKAOS_TLS_THREAD_SLOTS) {
        pthread_records_release();
        return EAGAIN;
    }
    pthread_tls_values[key][free_slot].thread = thread;
    pthread_tls_values[key][free_slot].value = (void *)value;
    pthread_records_release();
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return 0;
    pthread_t thread = pthread_self();
    if (!thread) return 0;
    pthread_records_acquire();
    if (!pthread_tls_keys[key].used) {
        pthread_records_release();
        return 0;
    }
    for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
        if (pthread_tls_values[key][slot].thread == thread) {
            void *value = pthread_tls_values[key][slot].value;
            pthread_records_release();
            return value;
        }
    }
    pthread_records_release();
    return 0;
}

static void pthread_run_destructors(void) {
    pthread_t thread = pthread_self();
    if (!thread) return;
    /* POSIX permits a destructor to publish another value while it runs;
     * repeat for the standard bounded number of passes instead of dropping
     * that value on thread exit. */
    for (uint32_t pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
        int ran = 0;
        for (uint32_t key = 0; key < PTHREAD_KEYS_MAX; ++key) {
            void *value = 0;
            void (*destructor)(void *) = 0;
            pthread_records_acquire();
            if (pthread_tls_keys[key].used) {
                for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
                    if (pthread_tls_values[key][slot].thread != thread) continue;
                    value = pthread_tls_values[key][slot].value;
                    destructor = pthread_tls_keys[key].destructor;
                    /* POSIX clears the value before invoking the destructor.
                     * The callback may then safely install a new value for a
                     * later destructor iteration. */
                    pthread_tls_values[key][slot].thread = 0;
                    pthread_tls_values[key][slot].value = 0;
                    break;
                }
            }
            pthread_records_release();
            if (!value) continue;
            ran = 1;
            if (destructor) destructor(value);
        }
        if (!ran) break;
    }
    /* Values republished by the final permitted destructor iteration are
     * discarded at thread exit, but their fixed table slots must be reclaimed. */
    pthread_records_acquire();
    for (uint32_t key = 0; key < PTHREAD_KEYS_MAX; ++key) {
        for (uint32_t slot = 0; slot < AKAOS_TLS_THREAD_SLOTS; ++slot) {
            if (pthread_tls_values[key][slot].thread != thread) continue;
            pthread_tls_values[key][slot].thread = 0;
            pthread_tls_values[key][slot].value = 0;
        }
    }
    pthread_records_release();
}

__attribute__((noreturn)) void pthread_exit(void *value) {
    pthread_t thread = pthread_self();
    pthread_run_cleanup_handlers();
    pthread_run_destructors();
    akaos_dl_static_tls_release();
    pthread_errno_release_thread(thread);
    akaos_dl_thread_cleanup(thread);
    (void)akaos_thread_syscall(AKAOS_SYSCALL_THREAD_EXIT,
                               (uint64_t)(uintptr_t)value, 0, 0);
    for (;;) (void)akaos_sched_yield();
}

/* The kernel enters this function directly on a new user stack.  It is
 * deliberately freestanding: all state needed by the thread lives in the
 * caller's shared address space and the return value is handed to the kernel
 * through THREAD_EXIT. */
__attribute__((noreturn)) static void pthread_start_thunk(
    struct pthread_start_context *context) {
    (void)akaos_dl_static_tls_prepare();
    void *value = 0;
    if (context && context->start_routine) {
        value = context->start_routine(context->argument);
    }
    pthread_exit(value);
}

/* The low bit is the lock owner state; a condition sequence is a monotonic
 * wake counter.  They are intentionally tiny and suitable for shared memory.
 */
#define PTHREAD_LOCKED 1U
#define PTHREAD_ONCE_RUNNING 1U
#define PTHREAD_ONCE_DONE 2U

void pthread_testcancel(void) {
    pthread_t self = pthread_self();
    if (!self) return;
    int state = PTHREAD_CANCEL_ENABLE;
    int type = PTHREAD_CANCEL_DEFERRED;
    uint32_t requested = 0U;
    int found = 0;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used || pthread_records[i].id != self)
            continue;
        state = pthread_records[i].cancel_state;
        type = pthread_records[i].cancel_type;
        requested = __atomic_load_n(&pthread_records[i].cancel_requested,
                                    __ATOMIC_ACQUIRE);
        if (requested && state == PTHREAD_CANCEL_ENABLE)
            __atomic_store_n(&pthread_records[i].cancel_requested, 0U,
                             __ATOMIC_RELEASE);
        found = 1;
        break;
    }
    if (!found) {
        state = pthread_main_cancel_state;
        type = pthread_main_cancel_type;
        requested = __atomic_load_n(&pthread_main_cancel_requested,
                                    __ATOMIC_ACQUIRE);
        if (requested && state == PTHREAD_CANCEL_ENABLE)
            __atomic_store_n(&pthread_main_cancel_requested, 0U,
                             __ATOMIC_RELEASE);
    }
    pthread_records_release();
    (void)type;
    if (!requested || state != PTHREAD_CANCEL_ENABLE) return;

    pthread_exit(PTHREAD_CANCELED);
}

int pthread_cancel(pthread_t thread) {
    if (!thread) return ESRCH;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used || pthread_records[i].id != thread)
            continue;
        __atomic_store_n(&pthread_records[i].cancel_requested, 1U,
                         __ATOMIC_RELEASE);
        int asynchronous = pthread_records[i].cancel_type ==
                           PTHREAD_CANCEL_ASYNCHRONOUS &&
                           pthread_records[i].cancel_state ==
                           PTHREAD_CANCEL_ENABLE;
        pthread_records_release();
        /* Asynchronous requests are delivered at the next safe scheduler
         * point; native XSCore does not interrupt a thread in arbitrary user
         * instructions. */
        (void)asynchronous;
        return 0;
    }
    if (thread == pthread_self()) {
        __atomic_store_n(&pthread_main_cancel_requested, 1U,
                         __ATOMIC_RELEASE);
        pthread_records_release();
        return 0;
    }
    pthread_records_release();
    return ESRCH;
}

int pthread_setcancelstate(int state, int *oldstate) {
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    pthread_t self = pthread_self();
    if (!self) return ESRCH;
    int previous = PTHREAD_CANCEL_ENABLE;
    int found = 0;
    uint32_t requested = 0U;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used || pthread_records[i].id != self)
            continue;
        previous = pthread_records[i].cancel_state;
        pthread_records[i].cancel_state = state;
        requested = __atomic_load_n(&pthread_records[i].cancel_requested,
                                    __ATOMIC_ACQUIRE);
        found = 1;
        break;
    }
    if (!found) {
        previous = pthread_main_cancel_state;
        pthread_main_cancel_state = state;
        requested = __atomic_load_n(&pthread_main_cancel_requested,
                                    __ATOMIC_ACQUIRE);
    }
    pthread_records_release();
    if (oldstate) *oldstate = previous;
    if (state == PTHREAD_CANCEL_ENABLE && requested)
        pthread_testcancel();
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
    if (type != PTHREAD_CANCEL_DEFERRED &&
        type != PTHREAD_CANCEL_ASYNCHRONOUS)
        return EINVAL;
    pthread_t self = pthread_self();
    if (!self) return ESRCH;
    int previous = PTHREAD_CANCEL_DEFERRED;
    int state = PTHREAD_CANCEL_ENABLE;
    uint32_t requested = 0U;
    int found = 0;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used || pthread_records[i].id != self)
            continue;
        previous = pthread_records[i].cancel_type;
        state = pthread_records[i].cancel_state;
        pthread_records[i].cancel_type = type;
        requested = __atomic_load_n(&pthread_records[i].cancel_requested,
                                    __ATOMIC_ACQUIRE);
        found = 1;
        break;
    }
    if (!found) {
        previous = pthread_main_cancel_type;
        state = pthread_main_cancel_state;
        pthread_main_cancel_type = type;
        requested = __atomic_load_n(&pthread_main_cancel_requested,
                                    __ATOMIC_ACQUIRE);
    }
    pthread_records_release();
    if (oldtype) *oldtype = previous;
    if (type == PTHREAD_CANCEL_ASYNCHRONOUS &&
        state == PTHREAD_CANCEL_ENABLE && requested)
        pthread_testcancel();
    return 0;
}

static void pthread_pause(void) {
    /* Yield through the dedicated scheduler ABI.  Unlike poll(2), this never
     * services or consumes the interactive TTY input queue. */
    pthread_reap_detached_records();
    (void)akaos_sched_yield();
    pthread_testcancel();
}

int pthread_attr_init(pthread_attr_t *attributes) {
    if (!attributes) return EINVAL;
    pthread_attr_stack_forget(attributes);
    attributes->flags = 0U;
    attributes->stack_size = AKAOS_PTHREAD_STACK_DEFAULT;
    attributes->detach_state = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attributes) {
    if (!attributes) return EINVAL;
    pthread_attr_stack_forget(attributes);
    attributes->flags = 0U;
    attributes->stack_size = 0U;
    attributes->detach_state = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size) {
    if (!attributes || size < AKAOS_PTHREAD_STACK_MINIMUM ||
        size > AKAOS_PTHREAD_STACK_MAXIMUM)
        return EINVAL;
    pthread_attr_stack_forget(attributes);
    attributes->stack_size = size;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attributes, size_t *size) {
    if (!attributes || !size) return EINVAL;
    *size = attributes->stack_size;
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attributes,
                          void **stack_address, size_t *stack_size) {
    if (!attributes || !stack_address || !stack_size) return EINVAL;
    *stack_address = NULL;
    *stack_size = attributes->stack_size;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_ATTR_STACK_RECORDS; ++i) {
        if (pthread_attr_stack_records[i].attributes != attributes) continue;
        *stack_address = pthread_attr_stack_records[i].address;
        *stack_size = pthread_attr_stack_records[i].size;
        break;
    }
    pthread_records_release();
    return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes) {
    if (!thread || !attributes) return EINVAL;

    void *stack_address = NULL;
    size_t stack_size = 0U;
    pthread_t self = pthread_self();
    if (thread == self && pthread_initial_stack_top) {
        stack_size = (size_t)AKAOS_INITIAL_STACK_PAGE_SIZE *
                     AKAOS_INITIAL_STACK_MAX_PAGES;
        if (pthread_initial_stack_top < stack_size) return EOVERFLOW;
        stack_address = (void *)(pthread_initial_stack_top - stack_size);
    } else {
        pthread_records_acquire();
        if (thread == __atomic_load_n(&pthread_initial_thread,
                                      __ATOMIC_ACQUIRE) &&
            pthread_initial_stack_top) {
            stack_size = (size_t)AKAOS_INITIAL_STACK_PAGE_SIZE *
                         AKAOS_INITIAL_STACK_MAX_PAGES;
            if (pthread_initial_stack_top >= stack_size)
                stack_address =
                    (void *)(pthread_initial_stack_top - stack_size);
        } else {
            for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
                if (!pthread_records[i].used ||
                    pthread_records[i].id != thread)
                    continue;
                stack_address = pthread_records[i].mapping;
                stack_size = pthread_records[i].length;
                break;
            }
        }
        pthread_records_release();
        if (!stack_address || !stack_size) return ESRCH;
    }

    attributes->flags = 0U;
    attributes->stack_size = stack_size;
    attributes->detach_state = PTHREAD_CREATE_JOINABLE;
    return pthread_attr_stack_remember(attributes, stack_address, stack_size);
}

int pthread_attr_setdetachstate(pthread_attr_t *attributes, int state) {
    if (!attributes || (state != PTHREAD_CREATE_JOINABLE &&
                        state != PTHREAD_CREATE_DETACHED))
        return EINVAL;
    attributes->detach_state = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attributes, int *state) {
    if (!attributes || !state) return EINVAL;
    *state = attributes->detach_state;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->type = PTHREAD_MUTEX_NORMAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->type = PTHREAD_MUTEX_NORMAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type) {
    if (!attributes || (type != PTHREAD_MUTEX_NORMAL &&
                        type != PTHREAD_MUTEX_ERRORCHECK &&
                        type != PTHREAD_MUTEX_RECURSIVE))
        return EINVAL;
    attributes->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attributes, int *type) {
    if (!attributes || !type) return EINVAL;
    *type = attributes->type;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attributes, int pshared) {
    if (!attributes || (pshared != PTHREAD_PROCESS_PRIVATE &&
                        pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    attributes->pshared = pshared;
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attributes,
                                 int *pshared) {
    if (!attributes || !pshared) return EINVAL;
    *pshared = attributes->pshared;
    return 0;
}

int sched_yield(void) {
    pthread_pause();
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attributes) {
    if (!mutex) return EINVAL;
    int pshared = PTHREAD_PROCESS_PRIVATE;
    if (attributes) {
        pshared = ((const pthread_mutexattr_t *)attributes)->pshared;
        if (pshared != PTHREAD_PROCESS_PRIVATE &&
            pshared != PTHREAD_PROCESS_SHARED)
            return EINVAL;
    }
    __atomic_store_n(&mutex->state, 0U, __ATOMIC_RELAXED);
    mutex->owner = 0U;
    mutex->recursion = 0U;
    mutex->type = attributes
        ? ((const pthread_mutexattr_t *)attributes)->type
        : PTHREAD_MUTEX_NORMAL;
    if (mutex->type != PTHREAD_MUTEX_NORMAL &&
        mutex->type != PTHREAD_MUTEX_ERRORCHECK &&
        mutex->type != PTHREAD_MUTEX_RECURSIVE)
        mutex->type = PTHREAD_MUTEX_NORMAL;
    mutex->pshared = pshared;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    if (__atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE) != 0U) return EBUSY;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    pthread_t self = pthread_self();
    uint32_t held = __atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE);
    if (held != 0U && mutex->owner == self) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            if (mutex->recursion == UINT32_MAX) return EBUSY;
            ++mutex->recursion;
            return 0;
        }
        return EBUSY;
    }
    uint32_t expected = 0U;
    if (__atomic_compare_exchange_n(&mutex->state, &expected, PTHREAD_LOCKED,
                                    0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        mutex->owner = self;
        mutex->recursion = 1U;
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    for (;;) {
        if (pthread_mutex_trylock(mutex) == 0) return 0;
        uint32_t state = __atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE);
        if (state != 0U)
            (void)akaos_futex(
                &mutex->state,
                akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                      mutex->pshared == PTHREAD_PROCESS_SHARED),
                state);
    }
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    pthread_t self = pthread_self();
    if (__atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE) == 0U ||
        (mutex->owner != self && mutex->type != PTHREAD_MUTEX_NORMAL))
        return EPERM;
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->recursion > 1U) {
        --mutex->recursion;
        return 0;
    }
    mutex->owner = 0U;
    mutex->recursion = 0U;
    uint32_t old_state =
        __atomic_exchange_n(&mutex->state, 0U, __ATOMIC_RELEASE);
    if (old_state != 0U)
        (void)akaos_futex(
            &mutex->state,
            akaos_futex_operation(AKAOS_FUTEX_WAKE,
                                  mutex->pshared == PTHREAD_PROCESS_SHARED),
            1U);
    return 0;
}

int pthread_cond_init(pthread_cond_t *condition, const void *attributes) {
    if (!condition) return EINVAL;
    if (attributes && ((const pthread_condattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_PRIVATE &&
        ((const pthread_condattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_SHARED)
        return EINVAL;
    const pthread_condattr_t *condition_attributes = attributes;
    int clock_id = condition_attributes
        ? condition_attributes->clock_id : CLOCK_REALTIME;
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
        return EINVAL;
    __atomic_store_n(&condition->sequence, 0U, __ATOMIC_RELAXED);
    condition->pshared = attributes
        ? condition_attributes->pshared
        : PTHREAD_PROCESS_PRIVATE;
    condition->clock_id = clock_id;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *condition) {
    return condition ? 0 : EINVAL;
}

int pthread_condattr_init(pthread_condattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    attributes->clock_id = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    attributes->clock_id = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *attributes, int pshared) {
    if (!attributes || (pshared != PTHREAD_PROCESS_PRIVATE &&
                        pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    attributes->pshared = pshared;
    return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *attributes,
                                int *pshared) {
    if (!attributes || !pshared) return EINVAL;
    *pshared = attributes->pshared;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attributes, int clock_id) {
    if (!attributes || (clock_id != CLOCK_REALTIME &&
                        clock_id != CLOCK_MONOTONIC))
        return EINVAL;
    attributes->clock_id = clock_id;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attributes,
                              int *clock_id) {
    if (!attributes || !clock_id) return EINVAL;
    *clock_id = attributes->clock_id;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *condition) {
    if (!condition) return EINVAL;
    (void)__atomic_add_fetch(&condition->sequence, 1U, __ATOMIC_RELEASE);
    (void)akaos_futex(
        &condition->sequence,
        akaos_futex_operation(AKAOS_FUTEX_WAKE,
                              condition->pshared == PTHREAD_PROCESS_SHARED),
        1U);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *condition) {
    if (!condition) return EINVAL;
    (void)__atomic_add_fetch(&condition->sequence, 1U, __ATOMIC_RELEASE);
    (void)akaos_futex(
        &condition->sequence,
        akaos_futex_operation(AKAOS_FUTEX_WAKE,
                              condition->pshared == PTHREAD_PROCESS_SHARED),
        UINT32_MAX);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
    if (!condition || !mutex) return EINVAL;
    uint32_t sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) return result;
    while (__atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) == sequence) {
        long waited = akaos_futex(
            &condition->sequence,
            akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                  condition->pshared == PTHREAD_PROCESS_SHARED),
            sequence);
        if (waited < 0 && waited != -EAGAIN) {
            (void)pthread_mutex_lock(mutex);
            return (int)-waited;
        }
    }
    return pthread_mutex_lock(mutex);
}

int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    if (!condition || !mutex || !abstime || abstime->tv_sec < 0 ||
        abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    uint32_t sequence =
        __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) return result;

    for (;;) {
        if (__atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) !=
            sequence) {
            result = pthread_mutex_lock(mutex);
            return result;
        }
        struct timespec now;
        if (clock_gettime(condition->clock_id, &now) < 0) {
            (void)pthread_mutex_lock(mutex);
            return EINVAL;
        }
        if (now.tv_sec > abstime->tv_sec ||
            (now.tv_sec == abstime->tv_sec &&
             now.tv_nsec >= abstime->tv_nsec)) {
            result = pthread_mutex_lock(mutex);
            if (result != 0) return result;
            return __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) !=
                           sequence ? 0 : ETIMEDOUT;
        }
        struct timespec remaining = {
            .tv_sec = abstime->tv_sec - now.tv_sec,
            .tv_nsec = abstime->tv_nsec - now.tv_nsec
        };
        if (remaining.tv_nsec < 0) {
            --remaining.tv_sec;
            remaining.tv_nsec += 1000000000L;
        }
        long waited = akaos_futex_timed(
            &condition->sequence,
            akaos_futex_operation(AKAOS_FUTEX_WAIT_TIMEOUT,
                                  condition->pshared ==
                                      PTHREAD_PROCESS_SHARED),
            sequence, &remaining);
        if (waited == -ETIMEDOUT) {
            result = pthread_mutex_lock(mutex);
            if (result != 0) return result;
            return __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) !=
                           sequence ? 0 : ETIMEDOUT;
        }
        if (waited < 0 && waited != -EAGAIN) {
            (void)pthread_mutex_lock(mutex);
            return (int)-waited;
        }
    }
}

/* A compact reader/writer lock.  The high bit reserves the lock for one
 * writer; the low 31 bits count active readers.  All transitions are atomic
 * and waiters yield through the native scheduler ABI. */
#define PTHREAD_RWLOCK_WRITER UINT32_C(0x80000000)
#define PTHREAD_RWLOCK_READERS UINT32_C(0x7fffffff)

int pthread_rwlock_init(pthread_rwlock_t *lock, const void *attributes) {
    if (!lock) return EINVAL;
    if (attributes && ((const pthread_rwlockattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_PRIVATE &&
        ((const pthread_rwlockattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_SHARED)
        return EINVAL;
    __atomic_store_n(&lock->state, 0U, __ATOMIC_RELAXED);
    lock->pshared = attributes
        ? ((const pthread_rwlockattr_t *)attributes)->pshared
        : PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    return __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE) == 0U ? 0 : EBUSY;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attributes,
                                  int pshared) {
    if (!attributes || (pshared != PTHREAD_PROCESS_PRIVATE &&
                        pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    attributes->pshared = pshared;
    return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attributes,
                                  int *pshared) {
    if (!attributes || !pshared) return EINVAL;
    *pshared = attributes->pshared;
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    uint32_t state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
    for (;;) {
        if (state & PTHREAD_RWLOCK_WRITER ||
            (state & PTHREAD_RWLOCK_READERS) == PTHREAD_RWLOCK_READERS)
            return EBUSY;
        uint32_t desired = state + 1U;
        if (__atomic_compare_exchange_n(&lock->state, &state, desired, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
    }
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    while (pthread_rwlock_tryrdlock(lock) != 0) {
        uint32_t state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
        (void)akaos_futex(
            &lock->state,
            akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                  lock->pshared == PTHREAD_PROCESS_SHARED),
            state);
    }
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    uint32_t expected = 0U;
    return __atomic_compare_exchange_n(&lock->state, &expected,
                                       PTHREAD_RWLOCK_WRITER, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
               ? 0 : EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    while (pthread_rwlock_trywrlock(lock) != 0) {
        uint32_t state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
        (void)akaos_futex(
            &lock->state,
            akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                  lock->pshared == PTHREAD_PROCESS_SHARED),
            state);
    }
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
    if (!lock) return EINVAL;
    uint32_t state = __atomic_load_n(&lock->state, __ATOMIC_ACQUIRE);
    if (state & PTHREAD_RWLOCK_WRITER) {
        uint32_t expected = PTHREAD_RWLOCK_WRITER;
        if (!__atomic_compare_exchange_n(&lock->state, &expected, 0U, 0,
                                         __ATOMIC_RELEASE,
                                         __ATOMIC_RELAXED)) return EPERM;
        (void)akaos_futex(
            &lock->state,
            akaos_futex_operation(AKAOS_FUTEX_WAKE,
                                  lock->pshared == PTHREAD_PROCESS_SHARED),
            UINT32_MAX);
        return 0;
    }
    for (;;) {
        if ((state & PTHREAD_RWLOCK_READERS) == 0U) return EPERM;
        uint32_t desired = state - 1U;
        if (__atomic_compare_exchange_n(&lock->state, &state, desired, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            (void)akaos_futex(
                &lock->state,
                akaos_futex_operation(AKAOS_FUTEX_WAKE,
                                      lock->pshared == PTHREAD_PROCESS_SHARED),
                UINT32_MAX);
            return 0;
        }
    }
}

/* A bounded reusable barrier.  The low 31 bits of state count arrivals and
 * the high bit serializes the short update section; generation changes when
 * the final participant arrives.  Waiters yield through XSCore rather than
 * spinning, so this remains usable by resident toolkit worker pools. */
#define PTHREAD_BARRIER_LOCK UINT32_C(0x80000000)
#define PTHREAD_BARRIER_COUNT_MASK UINT32_C(0x7fffffff)

int pthread_barrierattr_init(pthread_barrierattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attributes) {
    if (!attributes) return EINVAL;
    attributes->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *attributes,
                                   int pshared) {
    if (!attributes || (pshared != PTHREAD_PROCESS_PRIVATE &&
                        pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    attributes->pshared = pshared;
    return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attributes,
                                   int *pshared) {
    if (!attributes || !pshared) return EINVAL;
    *pshared = attributes->pshared;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *barrier,
                         const void *attributes, unsigned count) {
    if (!barrier || count == 0U || count > PTHREAD_BARRIER_COUNT_MASK)
        return EINVAL;
    if (attributes && ((const pthread_barrierattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_PRIVATE &&
        ((const pthread_barrierattr_t *)attributes)->pshared !=
                      PTHREAD_PROCESS_SHARED)
        return EINVAL;
    __atomic_store_n(&barrier->state, 0U, __ATOMIC_RELAXED);
    barrier->trip_count = count;
    __atomic_store_n(&barrier->generation, 0U, __ATOMIC_RELAXED);
    barrier->pshared = attributes
        ? ((const pthread_barrierattr_t *)attributes)->pshared
        : PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    if (!barrier || barrier->trip_count == 0U) return EINVAL;
    if (__atomic_load_n(&barrier->state, __ATOMIC_ACQUIRE) != 0U)
        return EBUSY;
    barrier->trip_count = 0U;
    __atomic_store_n(&barrier->generation, 0U, __ATOMIC_RELAXED);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    if (!barrier || barrier->trip_count == 0U) return EINVAL;
    uint32_t observed;
    for (;;) {
        observed = __atomic_load_n(&barrier->state, __ATOMIC_ACQUIRE);
        if (observed & PTHREAD_BARRIER_LOCK) {
            pthread_pause();
            continue;
        }
        uint32_t locked = observed | PTHREAD_BARRIER_LOCK;
        if (__atomic_compare_exchange_n(&barrier->state, &observed, locked, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            break;
    }

    uint32_t generation =
        __atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE);
    uint32_t arrivals = observed & PTHREAD_BARRIER_COUNT_MASK;
    if (arrivals + 1U >= barrier->trip_count) {
        __atomic_store_n(&barrier->state, 0U, __ATOMIC_RELEASE);
        (void)__atomic_add_fetch(&barrier->generation, 1U, __ATOMIC_RELEASE);
        (void)akaos_futex(
            &barrier->generation,
            akaos_futex_operation(AKAOS_FUTEX_WAKE,
                                  barrier->pshared == PTHREAD_PROCESS_SHARED),
            UINT32_MAX);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    __atomic_store_n(&barrier->state, arrivals + 1U, __ATOMIC_RELEASE);
    while (__atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE) == generation)
        (void)akaos_futex(
            &barrier->generation,
            akaos_futex_operation(AKAOS_FUTEX_WAIT,
                                  barrier->pshared == PTHREAD_PROCESS_SHARED),
            generation);
    return 0;
}

int pthread_once(pthread_once_t *once, void (*initializer)(void)) {
    if (!once || !initializer) return EINVAL;
    uint32_t expected = 0U;
    if (__atomic_compare_exchange_n(&once->state, &expected,
                                    PTHREAD_ONCE_RUNNING, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
        initializer();
        __atomic_store_n(&once->state, PTHREAD_ONCE_DONE, __ATOMIC_RELEASE);
        (void)akaos_futex(
            &once->state,
            akaos_futex_operation(AKAOS_FUTEX_WAKE, 0), UINT32_MAX);
        return 0;
    }
    for (;;) {
        uint32_t state = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (state == PTHREAD_ONCE_DONE) return 0;
        (void)akaos_futex(&once->state,
                          akaos_futex_operation(AKAOS_FUTEX_WAIT, 0), state);
    }
}

pthread_t pthread_self(void) {
    long tid = akaos_thread_syscall(AKAOS_SYSCALL_GETTID, 0, 0, 0);
    if (tid < 0) return 0U;
    pthread_t value = (pthread_t)tid;
    pthread_t expected = 0U;
    (void)__atomic_compare_exchange_n(&pthread_initial_thread, &expected,
                                      value, 0, __ATOMIC_ACQ_REL,
                                      __ATOMIC_ACQUIRE);
    return value;
}

int pthread_equal(pthread_t left, pthread_t right) {
    return left == right;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*start_routine)(void *), void *argument) {
    if (!thread || !start_routine) return EINVAL;
    /* Establish the initial-thread identity before the new task can run, so
     * cleanup handlers registered by the caller never get mistaken for a
     * worker whose record has not been published yet. */
    (void)pthread_self();
    size_t stack_size = AKAOS_PTHREAD_STACK_DEFAULT;
    int detached = 0;
    if (attributes) {
        if (attributes->stack_size != 0U) {
            if (attributes->stack_size < AKAOS_PTHREAD_STACK_MINIMUM ||
                attributes->stack_size > AKAOS_PTHREAD_STACK_MAXIMUM)
                return EINVAL;
            stack_size = attributes->stack_size;
        }
        if (attributes->detach_state == PTHREAD_CREATE_DETACHED) detached = 1;
        else if (attributes->detach_state != PTHREAD_CREATE_JOINABLE)
            return EINVAL;
    }
    pthread_reap_detached_records();
    pthread_records_acquire();
    int have_record = 0;
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used) {
            have_record = 1;
            break;
        }
    }
    pthread_records_release();
    if (!have_record) return EAGAIN;
    uint8_t *mapping = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return ENOMEM;
    struct pthread_start_context *context =
        (struct pthread_start_context *)(void *)mapping;
    context->start_routine = start_routine;
    context->argument = argument;
    uintptr_t top = (uintptr_t)mapping + stack_size;
    uintptr_t stack;
#if defined(__x86_64__)
    stack = (top & ~(uintptr_t)15U) - 8U;
    *(uint64_t *)(void *)stack = 0;
#elif defined(__i386__)
    /* Keep both the synthetic return slot and the cdecl argument inside the
     * mapped range; the kernel writes the argument at stack+4. */
    stack = (top & ~(uintptr_t)3U) - 8U;
    *(uint32_t *)(void *)stack = 0;
#else
    /* Keep the initial SP inside the mapped stack.  AAPCS/RISC-V require
     * 16-byte alignment at a C entry point; the thunk never returns, so no
     * synthetic link slot is needed. */
    stack = (top & ~(uintptr_t)15U) - 16U;
#endif
    long result = akaos_thread_syscall(
        AKAOS_SYSCALL_THREAD_CREATE,
        (uint64_t)(uintptr_t)&pthread_start_thunk,
        (uint64_t)stack, (uint64_t)(uintptr_t)context);
    if (result < 0) {
        (void)munmap(mapping, stack_size);
        return (int)-result;
    }
    *thread = (pthread_t)result;
    int recorded = 0;
    uint32_t record_index = 0;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (pthread_records[i].used) continue;
        pthread_records[i].id = *thread;
        pthread_records[i].mapping = mapping;
        pthread_records[i].length = stack_size;
        pthread_records[i].used = 1;
        pthread_records[i].detached = detached ? 1U : 0U;
        __atomic_store_n(&pthread_records[i].cancel_requested, 0U,
                         __ATOMIC_RELAXED);
        pthread_records[i].cancel_state = PTHREAD_CANCEL_ENABLE;
        pthread_records[i].cancel_type = PTHREAD_CANCEL_DEFERRED;
        pthread_cleanup_depth[i] = 0U;
        memset(pthread_cleanup_entries[i], 0,
               sizeof(pthread_cleanup_entries[i]));
        record_index = i;
        recorded = 1;
        break;
    }
    pthread_records_release();
    if (!recorded) {
        (void)akaos_thread_syscall(AKAOS_SYSCALL_THREAD_DETACH,
                                   (uint64_t)*thread, 0, 0);
        return EAGAIN;
    }
    if (detached) {
        /* Publish the record before detaching so an immediately exiting
         * worker can still be reclaimed by the normal detached sweep. */
        long detach_result = akaos_thread_syscall(
            AKAOS_SYSCALL_THREAD_DETACH, (uint64_t)*thread, 0, 0);
        if (detach_result < 0) {
            pthread_records_acquire();
            if (pthread_records[record_index].used &&
                pthread_records[record_index].id == *thread)
                pthread_records[record_index].detached = 0U;
            pthread_records_release();
        }
    }
    return 0;
}

int pthread_join(pthread_t thread, void **value) {
    pthread_reap_detached_records();
    uint64_t result_value = 0;
    long result = akaos_thread_syscall(
        AKAOS_SYSCALL_THREAD_JOIN, (uint64_t)thread,
        (uint64_t)(uintptr_t)&result_value, 0);
    if (result < 0) return (int)-result;
    if (value) *value = (void *)(uintptr_t)result_value;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (!pthread_records[i].used || pthread_records[i].id != thread) continue;
        void *mapping = pthread_records[i].mapping;
        size_t length = pthread_records[i].length;
        pthread_records[i].used = 0;
        pthread_records[i].detached = 0;
        pthread_cleanup_depth[i] = 0U;
        memset(pthread_cleanup_entries[i], 0,
               sizeof(pthread_cleanup_entries[i]));
        pthread_records_release();
        (void)munmap(mapping, length);
        return 0;
    }
    pthread_records_release();
    return 0;
}

int pthread_detach(pthread_t thread) {
    long result = akaos_thread_syscall(AKAOS_SYSCALL_THREAD_DETACH,
                                       (uint64_t)thread, 0, 0);
    if (result < 0) return (int)-result;
    pthread_records_acquire();
    for (uint32_t i = 0; i < AKAOS_PTHREAD_RECORDS; ++i) {
        if (pthread_records[i].used && pthread_records[i].id == thread) {
            pthread_records[i].detached = 1;
            break;
        }
    }
    pthread_records_release();
    pthread_reap_detached_records();
    return 0;
}
