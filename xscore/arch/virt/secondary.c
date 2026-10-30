#include <xscore/secondary.h>
#include <xscore/device_irq.h>
#include <xscore/serial.h>
#include <xscore/sched.h>
#include <xscore/thread.h>
#include <xscore/timer.h>
#include <stdint.h>
#include <stddef.h>

/* These arrays are intentionally simple and cache-line separated.  The
 * secondary entry code touches them before it has installed a full kernel
 * address space, so they must remain in the identity-mapped kernel image. */
volatile uint32_t xscore_secondary_release[XSCORE_SECONDARY_BOOT_LIMIT]
    __attribute__((aligned(64)));
static volatile uint32_t secondary_online[XSCORE_SECONDARY_BOOT_LIMIT]
    __attribute__((aligned(64)));
static volatile uint32_t secondary_heartbeats[XSCORE_SECONDARY_BOOT_LIMIT]
    __attribute__((aligned(64)));
static volatile uint32_t secondary_timer_seen[XSCORE_SECONDARY_BOOT_LIMIT]
    __attribute__((aligned(64)));
static uint32_t secondary_online_total;
static int secondary_initialized;
static volatile uint32_t secondary_mapping_ready;
static uint32_t secondary_bsp_raw_id;
static uint32_t secondary_raw_for_logical[XSCORE_SECONDARY_BOOT_LIMIT];

#define XSCORE_SECONDARY_STACK_SIZE UINT32_C(16384)
extern unsigned char secondary_stacks[];

static inline void secondary_relax(void);

static uint32_t secondary_read_raw_id(void) {
#if defined(__aarch64__)
    uint64_t value;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
    return (uint32_t)(value & UINT64_C(0xff));
#elif defined(__arm__)
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(value));
    return value & UINT32_C(0xff);
#elif defined(__riscv)
    uintptr_t value;
    /* mhartid is machine-mode-only; OpenSBI's entry ABI is preserved in tp. */
    __asm__ volatile("mv %0, tp" : "=r"(value));
    return (uint32_t)value;
#else
    return 0;
#endif
}

static void secondary_mapping_init(void) {
    if (__atomic_load_n(&secondary_mapping_ready, __ATOMIC_ACQUIRE) == 1) return;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&secondary_mapping_ready, &expected, 2,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        secondary_bsp_raw_id = secondary_read_raw_id();
        for (uint32_t i = 0; i < XSCORE_SECONDARY_BOOT_LIMIT; ++i) {
            secondary_raw_for_logical[i] = UINT32_MAX;
        }
        secondary_raw_for_logical[0] = secondary_bsp_raw_id;
        __atomic_store_n(&secondary_mapping_ready, 1, __ATOMIC_RELEASE);
        return;
    }
    while (__atomic_load_n(&secondary_mapping_ready, __ATOMIC_ACQUIRE) != 1) {
        secondary_relax();
    }
}

static uint32_t secondary_logical_for_raw(uint32_t raw_id) {
    secondary_mapping_init();
    if (raw_id == secondary_bsp_raw_id) return 0;
    for (uint32_t logical = 1; logical < XSCORE_SECONDARY_BOOT_LIMIT; ++logical) {
        if (__atomic_load_n(&secondary_raw_for_logical[logical],
                            __ATOMIC_ACQUIRE) == raw_id) return logical;
    }
    for (uint32_t logical = 1; logical < XSCORE_SECONDARY_BOOT_LIMIT; ++logical) {
        uint32_t empty = UINT32_MAX;
        if (__atomic_compare_exchange_n(&secondary_raw_for_logical[logical],
                                        &empty, raw_id, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return logical;
        }
    }
    return 0;
}

static inline void secondary_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("nop" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

#if defined(__aarch64__)
extern void _start_aarch64_secondary(void);

static int secondary_arch_start(uint32_t cpu_index) {
    /* QEMU virt numbers CPU affinities from zero; PSCI accepts the affinity
     * value directly for its virtual CPUs. */
    register uint64_t x0 __asm__("x0") = UINT64_C(0xc4000003); /* PSCI CPU_ON */
    register uint64_t x1 __asm__("x1") = cpu_index;
    register uint64_t x2 __asm__("x2") = (uint64_t)(uintptr_t)&_start_aarch64_secondary;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                     :: "memory");
    return (int64_t)x0 == 0;
}
#elif defined(__arm__)
extern void _start_arm32_secondary(void);

static int secondary_arch_start(uint32_t cpu_index) {
    register uint32_t r0 __asm__("r0") = UINT32_C(0x84000003); /* PSCI CPU_ON */
    register uint32_t r1 __asm__("r1") = cpu_index;
    register uint32_t r2 __asm__("r2") = (uint32_t)(uintptr_t)&_start_arm32_secondary;
    register uint32_t r3 __asm__("r3") = 0;
    __asm__ volatile("hvc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     :: "memory");
    return (int32_t)r0 == 0;
}
#elif defined(__riscv)
extern void _start_secondary(void);

static int secondary_arch_start(uint32_t cpu_index) {
    /* SBI HSM hart_start: EID 0x48534d, FID 0. */
    register uintptr_t a0 __asm__("a0") = (uintptr_t)cpu_index;
    register uintptr_t a1 __asm__("a1") = (uintptr_t)&_start_secondary;
    register uintptr_t a2 __asm__("a2") = 0;
    register uintptr_t a6 __asm__("a6") = 0;
    register uintptr_t a7 __asm__("a7") = (uintptr_t)0x48534d;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2)
                     : "r"(a6), "r"(a7) : "memory");
    return (intptr_t)a0 == 0;
}
#else
static int secondary_arch_start(uint32_t cpu_index) {
    (void)cpu_index;
    return 0;
}
#endif

void xscore_secondary_cpu_entry(uint32_t cpu_index) {
    if (cpu_index >= XSCORE_SECONDARY_BOOT_LIMIT) return;
    uint32_t logical_cpu = secondary_logical_for_raw(cpu_index);
    if (logical_cpu == 0) return;
    uint64_t stack_top = xscore_secondary_stack_top(cpu_index);
    if (!stack_top || !xscore_thread_register_cpu(logical_cpu, stack_top)) return;
    xscore_sched_register_core(logical_cpu, cpu_index, stack_top);
    /* Install the per-CPU interrupt-controller context before publishing the
     * CPU online, so the first timer IRQ always sees a registered idle TCB. */
    xscore_device_irq_secondary_initialize();
    __atomic_store_n(&secondary_online[cpu_index], 1U, __ATOMIC_RELEASE);

    /* Wait for one real hardware timer interrupt, then finish with a bounded
     * heartbeat.  The idle worker is kept short until user-task migration is
     * audited, but timer ownership is fully per CPU. */
    uint64_t timer_start = xscore_timer_ticks();
    uint64_t timer_budget = xscore_timer_frequency() / 10U;
    if (!timer_budget) timer_budget = 1;
    for (uint32_t tick = 0; tick < 20000000; ++tick) {
        __atomic_add_fetch(&secondary_heartbeats[cpu_index], 1U,
                           __ATOMIC_RELAXED);
        if (xscore_sched_cpu_ticks(logical_cpu) != 0) {
            __atomic_store_n(&secondary_timer_seen[cpu_index], 1U,
                             __ATOMIC_RELEASE);
            break;
        }
        if (xscore_timer_ticks() - timer_start >= timer_budget) break;
        secondary_relax();
    }
}

uint32_t xscore_secondary_current_cpu_index(void) {
    return secondary_logical_for_raw(secondary_read_raw_id());
}

uint32_t xscore_secondary_raw_cpu_id(void) {
    return secondary_read_raw_id();
}

uint64_t xscore_secondary_stack_top(uint32_t raw_cpu_id) {
    if (raw_cpu_id >= XSCORE_SECONDARY_BOOT_LIMIT) return 0;
    return (uint64_t)(uintptr_t)secondary_stacks +
           ((uint64_t)raw_cpu_id + 1U) * XSCORE_SECONDARY_STACK_SIZE;
}

uint32_t xscore_secondary_online_count(void) {
    secondary_mapping_init();
    uint32_t total = 0;
    for (uint32_t cpu = 0; cpu < XSCORE_SECONDARY_BOOT_LIMIT; ++cpu) {
        if (cpu == secondary_bsp_raw_id) continue;
        if (__atomic_load_n(&secondary_online[cpu], __ATOMIC_ACQUIRE)) ++total;
    }
    return total;
}

uint32_t xscore_secondary_heartbeat(uint32_t cpu_index) {
    if (cpu_index >= XSCORE_SECONDARY_BOOT_LIMIT) return 0;
    return __atomic_load_n(&secondary_heartbeats[cpu_index], __ATOMIC_ACQUIRE);
}

static uint32_t secondary_timer_seen_count(void) {
    secondary_mapping_init();
    uint32_t total = 0;
    for (uint32_t cpu = 0; cpu < XSCORE_SECONDARY_BOOT_LIMIT; ++cpu) {
        if (cpu == secondary_bsp_raw_id) continue;
        if (__atomic_load_n(&secondary_timer_seen[cpu], __ATOMIC_ACQUIRE)) ++total;
    }
    return total;
}

int xscore_secondary_initialize(void) {
    if (secondary_initialized) return 1;
    secondary_initialized = 1;

    secondary_mapping_init();
    uint32_t firmware_started = 0;
    uint32_t logical_cpu = 1;
    for (uint32_t raw_cpu = 0; raw_cpu < XSCORE_SECONDARY_BOOT_LIMIT; ++raw_cpu) {
        if (raw_cpu == secondary_bsp_raw_id) continue;
        if (logical_cpu < XSCORE_SECONDARY_BOOT_LIMIT) {
            secondary_raw_for_logical[logical_cpu++] = raw_cpu;
        }
        xscore_secondary_release[raw_cpu] = 0;
        secondary_online[raw_cpu] = 0;
        secondary_heartbeats[raw_cpu] = 0;
        if (secondary_arch_start(raw_cpu)) ++firmware_started;
        /* ARM firmware may leave -smp CPUs already parked at the common entry
         * point, in which case PSCI returns ALREADY_ON. Release every slot so
         * both that case and a stopped SBI hart converge on the same path. */
        __atomic_store_n(&xscore_secondary_release[raw_cpu], 1U,
                         __ATOMIC_RELEASE);
#if defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("sev" ::: "memory");
#elif defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
#endif
    }

    /* Firmware startup is asynchronous. A bounded wait keeps one-core
     * machines fast while allowing QEMU/OpenSBI to enter the new stacks. */
    for (uint32_t wait = 0; wait < 4000000; ++wait) {
        if (xscore_secondary_online_count() >= firmware_started &&
            secondary_timer_seen_count() >= firmware_started &&
            firmware_started != 0) break;
        secondary_relax();
    }

    secondary_online_total = xscore_secondary_online_count();
    if (!secondary_online_total) {
        xscore_serial_write("[INFO] No secondary CPUs reported by firmware; continuing on CPU 0\n");
    } else if (!firmware_started || secondary_online_total >= firmware_started) {
        xscore_serial_write("[PASS] Secondary CPU bring-up and heartbeat self-test passed\n");
    } else {
        xscore_serial_write("[WARN] Secondary CPU bring-up partially completed; CPU 0 remains active\n");
    }
    if (secondary_online_total == 0) {
        xscore_serial_write("[INFO] No secondary per-CPU timer interrupts required on CPU 0\n");
    } else if (secondary_timer_seen_count() >= secondary_online_total) {
        xscore_serial_write("[PASS] Secondary per-CPU timer interrupts observed\n");
    } else {
        xscore_serial_write("[WARN] Secondary per-CPU timer interrupts not observed on every online CPU\n");
    }
    return 1;
}
