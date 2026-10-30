#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

__attribute__((visibility("default"))) uintptr_t __stack_chk_guard;
static uint32_t stack_chk_guard_state;

static uintptr_t stack_chk_guard_fallback(void) {
    struct timespec now = {0, 0};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t mixed = (uint64_t)(uintptr_t)&__stack_chk_guard;
    mixed ^= (uint64_t)(uint32_t)getpid() << 32;
    mixed ^= (uint64_t)now.tv_sec * UINT64_C(0x9e3779b97f4a7c15);
    mixed ^= (uint64_t)now.tv_nsec + UINT64_C(0x517cc1b727220a95);
    mixed ^= mixed >> 30;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31;
    return (uintptr_t)mixed;
}

__attribute__((visibility("hidden")))
void __akaos_stack_chk_guard_init(void) {
    uint32_t state = __atomic_load_n(&stack_chk_guard_state, __ATOMIC_ACQUIRE);
    if (state == 2U) return;
    if (state == 1U) {
        while (__atomic_load_n(&stack_chk_guard_state, __ATOMIC_ACQUIRE) == 1U) {
        }
        return;
    }

    uint32_t expected = 0U;
    if (!__atomic_compare_exchange_n(&stack_chk_guard_state, &expected, 1U, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&stack_chk_guard_state, __ATOMIC_ACQUIRE) == 1U) {
        }
        return;
    }

    uintptr_t guard = 0U;
    int have_random_device = 0;
    int descriptor = open("/dev/urandom", O_RDONLY);
    if (descriptor >= 0) {
        size_t received = 0U;
        while (received < sizeof(guard)) {
            ssize_t amount = read(descriptor, (unsigned char *)&guard + received,
                                  sizeof(guard) - received);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0) break;
            received += (size_t)amount;
        }
        (void)close(descriptor);
        have_random_device = received == sizeof(guard);
    }
    if (!have_random_device) guard = stack_chk_guard_fallback();

    guard &= ~(uintptr_t)0xffU;
    if (guard == 0U) guard = (uintptr_t)UINT32_C(0x9e377900);
    __stack_chk_guard = guard;
    __atomic_store_n(&stack_chk_guard_state, 2U, __ATOMIC_RELEASE);
}

__attribute__((noreturn, visibility("default")))
void __stack_chk_fail(void) {
    abort();
    __builtin_trap();
}

__attribute__((noreturn, visibility("default")))
void __stack_chk_fail_local(void) {
    __stack_chk_fail();
}

typedef void (*akaos_init_function)(void);

extern akaos_init_function __preinit_array_start[]
    __attribute__((weak, visibility("hidden")));
extern akaos_init_function __preinit_array_end[]
    __attribute__((weak, visibility("hidden")));
extern akaos_init_function __init_array_start[]
    __attribute__((weak, visibility("hidden")));
extern akaos_init_function __init_array_end[]
    __attribute__((weak, visibility("hidden")));

#if !defined(__arm__)
extern void __register_frame(const void *fde) __attribute__((weak));
extern const unsigned char __eh_frame_start[]
    __attribute__((weak, visibility("hidden")));
extern const unsigned char __eh_frame_end[]
    __attribute__((weak, visibility("hidden")));

static void register_eh_frames(void) {
    uintptr_t first = (uintptr_t)__eh_frame_start;
    uintptr_t last = (uintptr_t)__eh_frame_end;
    if (!__register_frame || first == 0U || last <= first) return;
    __register_frame(__eh_frame_start);
}
#else
static void register_eh_frames(void) {
    /* ARM32 uses the linker-provided __exidx_start/__exidx_end EHABI table. */
}
#endif

static void run_array(akaos_init_function *start, akaos_init_function *end) {
    uintptr_t first = (uintptr_t)start;
    uintptr_t last = (uintptr_t)end;
    if (first == 0U || last < first ||
        (last - first) % sizeof(*start) != 0U)
        return;
    for (akaos_init_function *entry = start; entry < end; ++entry) {
        if (*entry) (*entry)();
    }
}

void __akaos_libc_run_init_array(void) {
    __akaos_stack_chk_guard_init();
    register_eh_frames();
    run_array(__preinit_array_start, __preinit_array_end);
    run_array(__init_array_start, __init_array_end);
}
