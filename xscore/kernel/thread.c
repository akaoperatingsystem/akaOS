#include <xscore/arch.h>
#include <xscore/device_irq.h>
#include <xscore/heap.h>
#include <xscore/memory.h>
#include <xscore/sched.h>
#include <xscore/serial.h>
#include <xscore/stack.h>
#include <xscore/thread.h>
#include <xscore/vmm.h>
#include <akaos/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * XSCore kernel thread implementation (Phase 3 — Multitasking).
 *
 * Threads are stored in a fixed-size table (no dynamic list yet). The boot
 * thread (index 0) represents the context the kernel is already running in
 * when xscore_thread_initialize() is called. New threads get a freshly
 * allocated kernel stack and a fabricated saved context so that the first
 * context switch into them begins at thread_entry_trampoline.
 *
 * Scheduling policy: simple round-robin over READY threads. Timer ticks latch
 * a request after a time slice; a safe thread-context checkpoint performs the
 * deferred switch.
 */

#define PAGE_SIZE UINT64_C(4096)
#if defined(__i386__) || defined(__arm__) || (defined(__riscv) && __riscv_xlen == 32)
#define THREAD_STACK_REGION_BASE UINT64_C(0x20000000)
#elif (defined(__riscv) && __riscv_xlen == 64)
#define THREAD_STACK_REGION_BASE UINT64_C(0xffffffd000000000)
#else
#define THREAD_STACK_REGION_BASE UINT64_C(0xffff820000000000)
#endif
#define TIME_SLICE_TICKS 5

#include <xscore/smp.h>

static struct xscore_thread threads[XSCORE_MAX_THREADS];
static uint64_t next_thread_id = 1;
static uint32_t per_core_current_thread[SMP_MAX_CPUS];
static int threads_initialized;
static uint32_t current_slice_ticks[SMP_MAX_CPUS];
static uint32_t preemption_pending[SMP_MAX_CPUS];
/* One bit per TCB, partitioned by owner CPU.  Keeping this beside the TCB
 * table makes READY admission explicit without introducing a second mutable
 * queue object or allowing stale entries after reap. */
static uint64_t per_cpu_ready_mask[SMP_MAX_CPUS];
static xscore_spinlock_t thread_lock = XSCORE_SPINLOCK_INIT;
#if defined(__x86_64__) || defined(__i386__)
static uint8_t initial_fpu_state[512] __attribute__((aligned(16)));
#endif
#if defined(__i386__)
static int i386_x87_state_enabled;
#endif

static void initialize_fpu_state(uint8_t *state) {
#if defined(__x86_64__)
    if (!state) return;
    for (uint32_t i = 0; i < sizeof(initial_fpu_state); ++i) {
        state[i] = initial_fpu_state[i];
    }
#elif defined(__i386__)
    if (!state || !i386_x87_state_enabled) return;
    for (uint32_t i = 0; i < 108U; ++i) state[i] = initial_fpu_state[i];
#else
    (void)state;
#endif
}

/* Probe CPUID rather than assuming every 32-bit target has an x87 unit. */
#if defined(__i386__)
static int i386_cpu_has_x87(void) {
    uint32_t original_flags, toggled_flags;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r"(original_flags), "=&r"(toggled_flags)
        :
        : "cc", "memory");
    if (((original_flags ^ toggled_flags) & UINT32_C(0x00200000)) == 0U)
        return 0;

    uint32_t eax = 0, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 1U) return 0;
    eax = 1U;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & UINT32_C(1)) != 0U;
}
#endif

/* Enable the x87/SSE state contract before capturing per-thread state. */
#if defined(__x86_64__) || defined(__i386__)
static void enable_x86_fpu_state(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= UINT64_C(1) << 1; /* MP: wait instructions track x87 state. */
    cr0 &= ~((UINT64_C(1) << 2) | (UINT64_C(1) << 3)); /* EM=0, TS=0. */
#if defined(__i386__)
    cr0 |= UINT64_C(1) << 5; /* NE: report x87 errors through #MF. */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
#else
    uint64_t cr4;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (UINT64_C(1) << 9) | (UINT64_C(1) << 10); /* OSFXSR, OSXMMEXCPT. */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
#endif
}
#endif


static inline uint32_t current_thread_idx(void) {
    uint32_t core = xscore_smp_current_cpu_index();
    if (core >= SMP_MAX_CPUS) return 0;
    return per_core_current_thread[core];
}

static inline void set_current_thread_idx(uint32_t index) {
    uint32_t core = xscore_smp_current_cpu_index();
    if (core < SMP_MAX_CPUS) {
        per_core_current_thread[core] = index;
    }
}

#define current_thread_index (current_thread_idx())

/* Defined in entry.S. */
extern void xscore_context_switch(struct xscore_thread_context *old_context,
                                  const struct xscore_thread_context *new_context);

/* Forward declarations. */
static void thread_entry_trampoline(void);
static void allocate_thread_stack(struct xscore_thread *thread);
static void reap_detached_threads(void);
extern void xscore_process_user_thread_entry(struct xscore_process *process,
                                             uint64_t entry, uint64_t stack,
                                             uint64_t user_arg0);
extern struct xscore_fd_table *xscore_process_fd_table(
    struct xscore_process *process);

/*
 * Allocate a kernel stack for a thread in the high-half stack region.
 * Layout (low → high): [guard page, unmapped] [stack pages, RW+NX].
 * Sets thread->stack_base and thread->stack_top.
 */
static void allocate_thread_stack(struct xscore_thread *thread) {
    uint32_t slot = (uint32_t)(thread - threads);
    uint64_t slot_span = (uint64_t)(XSCORE_THREAD_STACK_PAGES + 1 +
                                    XSCORE_THREAD_PRIVILEGE_STACK_PAGES + 1) * PAGE_SIZE;
    uint64_t slot_base = THREAD_STACK_REGION_BASE + (uint64_t)slot * slot_span;
    uint64_t guard_address = slot_base;
    uint64_t stack_base = guard_address + PAGE_SIZE;
    thread->stack_base = stack_base;
    thread->stack_top = 0;
    thread->privilege_stack_base = 0;
    thread->privilege_stack_top = 0;
    for (uint32_t i = 0; i < XSCORE_THREAD_STACK_PAGES; ++i) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) {
            goto failed;
        }
        uint64_t virtual = stack_base + i * PAGE_SIZE;
        uint64_t flags = XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE;
        if (!xscore_vmm_map_page_in(xscore_vmm_root_table(), virtual, physical, flags)) {
            xscore_pmm_free_page(physical);
            goto failed;
        }
    }
    thread->stack_top = stack_base + XSCORE_THREAD_STACK_PAGES * PAGE_SIZE;

    uint64_t privilege_guard = slot_base + (XSCORE_THREAD_STACK_PAGES + 1) * PAGE_SIZE;
    uint64_t privilege_base = privilege_guard + PAGE_SIZE;
    thread->privilege_stack_base = privilege_base;
    for (uint32_t i = 0; i < XSCORE_THREAD_PRIVILEGE_STACK_PAGES; ++i) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) goto failed;
        uint64_t virtual = privilege_base + i * PAGE_SIZE;
        uint64_t flags = XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE;
        if (!xscore_vmm_map_page_in(xscore_vmm_root_table(), virtual, physical, flags)) {
            xscore_pmm_free_page(physical);
            goto failed;
        }
    }
    thread->privilege_stack_top =
        privilege_base + XSCORE_THREAD_PRIVILEGE_STACK_PAGES * PAGE_SIZE;
    return;

failed:
    for (uint32_t i = 0; i < XSCORE_THREAD_STACK_PAGES; ++i) {
        uint64_t physical = 0;
        uint64_t virtual = stack_base + i * PAGE_SIZE;
        if (xscore_vmm_unmap_page_in(xscore_vmm_root_table(), virtual, &physical) && physical) {
            xscore_pmm_free_page(physical);
        }
    }
    for (uint32_t i = 0; i < XSCORE_THREAD_PRIVILEGE_STACK_PAGES; ++i) {
        uint64_t physical = 0;
        uint64_t virtual = thread->privilege_stack_base + i * PAGE_SIZE;
        if (thread->privilege_stack_base &&
            xscore_vmm_unmap_page_in(xscore_vmm_root_table(), virtual, &physical) &&
            physical) xscore_pmm_free_page(physical);
    }
    thread->stack_base = 0;
    thread->stack_top = 0;
    thread->privilege_stack_base = 0;
    thread->privilege_stack_top = 0;
}

static void release_thread_stack(struct xscore_thread *thread) {
    if (!thread->stack_base) return;
    for (uint32_t i = 0; i < XSCORE_THREAD_STACK_PAGES; ++i) {
        uint64_t physical = 0;
        uint64_t virtual = thread->stack_base + i * PAGE_SIZE;
        if (xscore_vmm_unmap_page_in(xscore_vmm_root_table(), virtual, &physical) && physical) {
            xscore_pmm_free_page(physical);
        }
    }
    for (uint32_t i = 0; i < XSCORE_THREAD_PRIVILEGE_STACK_PAGES; ++i) {
        uint64_t physical = 0;
        uint64_t virtual = thread->privilege_stack_base + i * PAGE_SIZE;
        if (thread->privilege_stack_base &&
            xscore_vmm_unmap_page_in(xscore_vmm_root_table(), virtual, &physical) &&
            physical) xscore_pmm_free_page(physical);
    }
    thread->stack_base = 0;
    thread->stack_top = 0;
    thread->privilege_stack_base = 0;
    thread->privilege_stack_top = 0;
}

/*
 * The trampoline is the initial RIP of every newly created thread. After the
 * first context switch restores the fabricated context, control lands here
 * with interrupts enabled. It calls the thread's entry function, then exits.
 */
static void thread_entry_trampoline(void) {
    struct xscore_thread *self = &threads[current_thread_index];
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sti");
#elif defined(__aarch64__)
    __asm__ volatile("msr daifclr, #2");
#elif defined(__arm__)
    __asm__ volatile("cpsie i");
#elif defined(__riscv)
    __asm__ volatile("csrsi sstatus, 2");
#endif
    if (self->user_mode_thread) {
        xscore_process_user_thread_entry(self->process, self->user_entry,
                                         self->user_stack, self->user_arg0);
    } else if (self->entry) {
        self->entry(self->argument);
    }
    xscore_thread_exit();
    /* xscore_thread_exit never returns. */
    for (;;) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("hlt");
#elif defined(__aarch64__)
        __asm__ volatile("wfe");
#elif defined(__arm__) || defined(__riscv)
        __asm__ volatile("wfi");
#else
        __asm__ volatile("");
#endif
    }
}

/*
 * Fabricate the initial saved context for a new thread.
 */
static void fabricate_initial_context(struct xscore_thread *thread) {
#if defined(__i386__)
    uint32_t *top_slot = (uint32_t *)(uintptr_t)(thread->stack_top - 8);
    *top_slot = (uint32_t)(uintptr_t)thread_entry_trampoline;
    thread->context.edi = 0;
    thread->context.esi = 0;
    thread->context.ebx = 0;
    thread->context.ebp = 0;
    thread->context.eflags = 0x202;
    thread->context.eip = (uint32_t)(uintptr_t)thread_entry_trampoline;
    thread->context.esp = (uint32_t)(thread->stack_top - 8);
#elif defined(__aarch64__)
    thread->context.x19 = 0;
    thread->context.x20 = 0;
    thread->context.x21 = 0;
    thread->context.x22 = 0;
    thread->context.x23 = 0;
    thread->context.x24 = 0;
    thread->context.x25 = 0;
    thread->context.x26 = 0;
    thread->context.x27 = 0;
    thread->context.x28 = 0;
    thread->context.fp = 0;
    thread->context.lr = (uint64_t)(uintptr_t)thread_entry_trampoline;
    thread->context.sp = thread->stack_top - 16;
    /* A TCB can be reused after a previous user task exits.  Reset the
     * exception-return fields as well as the callee-saved registers so a
     * scheduler handoff that occurs before the first EL0 transition cannot
     * inherit stale ELR_EL1/SPSR_EL1/SP_EL0 state. */
    thread->context.elr_el1 = 0;
    thread->context.spsr_el1 = 0;
    thread->context.sp_el0 = 0;
#elif defined(__arm__)
    thread->context.r4 = 0;
    thread->context.r5 = 0;
    thread->context.r6 = 0;
    thread->context.r7 = 0;
    thread->context.r8 = 0;
    thread->context.r9 = 0;
    thread->context.r10 = 0;
    thread->context.r11 = 0;
    thread->context.lr = (uint32_t)(uintptr_t)thread_entry_trampoline;
    thread->context.sp = (uint32_t)(thread->stack_top - 8);
#elif defined(__riscv)
    thread->context.s0 = 0;
    thread->context.s1 = 0;
    thread->context.s2 = 0;
    thread->context.s3 = 0;
    thread->context.s4 = 0;
    thread->context.s5 = 0;
    thread->context.s6 = 0;
    thread->context.s7 = 0;
    thread->context.s8 = 0;
    thread->context.s9 = 0;
    thread->context.s10 = 0;
    thread->context.s11 = 0;
    thread->context.ra = (uintptr_t)thread_entry_trampoline;
    thread->context.sp = (uintptr_t)(thread->stack_top - 16);
    thread->context.sepc = 0;
    thread->context.sstatus = 0;
    thread->context.sscratch = 0;
#else
    uint64_t *top_slot = (uint64_t *)(uintptr_t)(thread->stack_top - 16);
    *top_slot = (uint64_t)(uintptr_t)thread_entry_trampoline;
    thread->context.r15 = 0;
    thread->context.r14 = 0;
    thread->context.r13 = 0;
    thread->context.r12 = 0;
    thread->context.rbx = 0;
    thread->context.rbp = 0;
    thread->context.rflags = UINT64_C(0x202);
    thread->context.rip = (uint64_t)(uintptr_t)thread_entry_trampoline;
    thread->context.rsp = thread->stack_top - 16;
#endif
}

static uint32_t current_cpu(void) {
    uint32_t cpu = xscore_smp_current_cpu_index();
    return cpu < SMP_MAX_CPUS ? cpu : 0;
}

static void ready_mask_add_locked(uint32_t index) {
    if (index >= XSCORE_MAX_THREADS) return;
    uint32_t cpu = threads[index].owner_cpu;
    if (cpu < SMP_MAX_CPUS) {
        per_cpu_ready_mask[cpu] |= UINT64_C(1) << index;
    }
}

static void ready_mask_remove_locked(uint32_t index) {
    if (index >= XSCORE_MAX_THREADS) return;
    uint32_t cpu = threads[index].owner_cpu;
    if (cpu < SMP_MAX_CPUS) {
        per_cpu_ready_mask[cpu] &= ~(UINT64_C(1) << index);
    }
}

void xscore_thread_request_reschedule(void) {
    uint32_t cpu = current_cpu();
    if (cpu < SMP_MAX_CPUS) {
        __atomic_store_n(&preemption_pending[cpu], 1U, __ATOMIC_RELEASE);
    }
}

int xscore_thread_initialize(void) {
#if defined(__x86_64__)
    enable_x86_fpu_state();
    __asm__ volatile("fninit; fxsave %0" : "=m"(initial_fpu_state));
#elif defined(__i386__)
    i386_x87_state_enabled = i386_cpu_has_x87();
    if (i386_x87_state_enabled) {
        enable_x86_fpu_state();
        __asm__ volatile("fninit; fnsave %0" : "=m"(initial_fpu_state)
                         : : "memory");
    }
#endif
    for (uint32_t i = 0; i < XSCORE_MAX_THREADS; ++i) {
        threads[i].id = 0;
        threads[i].owner_cpu = 0;
        threads[i].state = XSCORE_THREAD_UNUSED;
        threads[i].fpu_initialized = 0;
    }
    /*
     * The boot context is the code currently running (the boot thread). It
     * does not have a freshly allocated stack — it reuses the dedicated boot
     * stack from xscore_stack_initialize(). Its context is filled in lazily
     * by the first context switch (the switch saves whatever is live).
     */
    threads[0].id = next_thread_id++;
    threads[0].owner_cpu = 0;
    threads[0].state = XSCORE_THREAD_RUNNING;
    threads[0].stack_top = 0;
    threads[0].stack_base = 0;
    threads[0].privilege_stack_base = 0;
    threads[0].privilege_stack_top = xscore_stack_syscall_top();
    threads[0].address_space_root = xscore_vmm_root_table();
    threads[0].file_descriptors = 0;
    threads[0].user_transition = (struct xscore_user_transition){0};
    initialize_fpu_state(threads[0].fpu_state);
    threads[0].fpu_initialized = 1;
    threads[0].entry = 0;
    threads[0].argument = 0;
    threads[0].process = 0;
    threads[0].user_entry = 0;
    threads[0].user_stack = 0;
    threads[0].user_arg0 = 0;
    threads[0].user_return_value = 0;
    threads[0].join_waiter_id = 0;
    threads[0].user_mode_thread = 0;
    threads[0].detached = 0;
    threads[0].wake_pending = 0;
    threads[0].exit_requested = 0;
    for (uint32_t c = 0; c < SMP_MAX_CPUS; ++c) {
        per_core_current_thread[c] = 0;
        current_slice_ticks[c] = 0;
        preemption_pending[c] = 0;
        per_cpu_ready_mask[c] = 0;
    }
    set_current_thread_idx(0);
    threads_initialized = 1;
    return 1;
}

uint64_t xscore_thread_create(void (*entry)(void *argument), void *argument) {
    return xscore_thread_create_in_address_space(entry, argument,
                                                 xscore_vmm_root_table());
}

uint64_t xscore_thread_create_in_address_space(void (*entry)(void *argument),
                                               void *argument,
                                               uint64_t address_space_root) {
    return xscore_thread_create_on_cpu(entry, argument, address_space_root,
                                       current_cpu());
}

uint64_t xscore_thread_create_on_cpu(void (*entry)(void *argument),
                                     void *argument,
                                     uint64_t address_space_root,
                                     uint32_t cpu_index) {
    if (!threads_initialized) return 0;
    if (!address_space_root) return 0;
    if (cpu_index >= SMP_MAX_CPUS || cpu_index >= xscore_smp_cpu_count()) return 0;

    uintptr_t irq_flags = xscore_spin_lock_irqsave(&thread_lock);
    uint32_t index = 0;
    for (index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].state == XSCORE_THREAD_UNUSED) break;
    }
    if (index >= XSCORE_MAX_THREADS) {
        xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
        return 0;
    }

    /* Reserve the slot before allocating pages so another CPU cannot select
     * the same TCB while the potentially slow VMM/PMM work is in progress. */
    struct xscore_thread *thread = &threads[index];
    thread->state = XSCORE_THREAD_ALLOCATING;
    thread->owner_cpu = cpu_index;
    xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
    allocate_thread_stack(thread);
    if (!thread->stack_top) {
        irq_flags = xscore_spin_lock_irqsave(&thread_lock);
        thread->owner_cpu = 0;
        thread->state = XSCORE_THREAD_UNUSED;
        xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
        return 0;
    }
    xscore_vmm_address_space_sync_kernel(address_space_root);

    irq_flags = xscore_spin_lock_irqsave(&thread_lock);
    thread->id = next_thread_id++;
    thread->address_space_root = address_space_root;
    thread->file_descriptors = 0;
    thread->user_transition = (struct xscore_user_transition){0};
    initialize_fpu_state(thread->fpu_state);
    thread->fpu_initialized = 1;
    thread->entry = entry;
    thread->argument = argument;
    thread->process = 0;
    thread->user_entry = 0;
    thread->user_stack = 0;
    thread->user_arg0 = 0;
    thread->user_return_value = 0;
    thread->join_waiter_id = 0;
    thread->wake_pending = 0;
    thread->exit_requested = 0;
    thread->user_mode_thread = 0;
    thread->detached = 0;
    fabricate_initial_context(thread);
    thread->state = XSCORE_THREAD_READY;
    ready_mask_add_locked(index);
    xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
    return thread->id;
}

int xscore_thread_register_cpu(uint32_t cpu_index, uint64_t privilege_stack_top) {
    if (!threads_initialized || cpu_index == 0 || cpu_index >= SMP_MAX_CPUS ||
        !privilege_stack_top) return 0;
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].state != XSCORE_THREAD_UNUSED) continue;
        struct xscore_thread *idle = &threads[index];
        idle->id = next_thread_id++;
        idle->owner_cpu = cpu_index;
        idle->state = XSCORE_THREAD_RUNNING;
        idle->stack_top = 0;
        idle->stack_base = 0;
        idle->privilege_stack_base = 0;
        idle->privilege_stack_top = privilege_stack_top;
        idle->address_space_root = xscore_vmm_root_table();
        idle->file_descriptors = 0;
        idle->user_transition = (struct xscore_user_transition){0};
        initialize_fpu_state(idle->fpu_state);
        idle->fpu_initialized = 1;
        idle->entry = 0;
        idle->argument = 0;
        idle->process = 0;
        idle->user_entry = 0;
        idle->user_stack = 0;
        idle->user_arg0 = 0;
        idle->user_return_value = 0;
        idle->join_waiter_id = 0;
        idle->user_mode_thread = 0;
        idle->detached = 0;
        idle->wake_pending = 0;
        idle->exit_requested = 0;
        ready_mask_remove_locked(index);
        per_core_current_thread[cpu_index] = index;
        xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
        return 1;
    }
    xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
    return 0;
}

/*
 * Find the next READY thread after the current one (round-robin).
 * Returns its index, or current_thread_index if none is runnable.
 */
static uint32_t find_next_runnable(void) {
    uint32_t cpu = current_cpu();
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    uint32_t current = current_thread_index;
    for (uint32_t offset = 1; offset < XSCORE_MAX_THREADS; ++offset) {
        uint32_t index = (current + offset) % XSCORE_MAX_THREADS;
        if (!(per_cpu_ready_mask[cpu] & (UINT64_C(1) << index))) continue;
        if (threads[index].state == XSCORE_THREAD_READY &&
            threads[index].owner_cpu == cpu) {
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return index;
        }
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return current;
}

static void switch_to(uint32_t next_index) {
    uintptr_t irq_flags = xscore_irq_save();
    if (next_index == current_thread_index) {
        xscore_irq_restore(irq_flags);
        return;
    }
    uint32_t old_index = current_thread_index;
    struct xscore_thread *old = &threads[old_index];
    struct xscore_thread *next = &threads[next_index];

    xscore_spin_lock(&thread_lock);
    if (old->state == XSCORE_THREAD_RUNNING) {
        old->state = XSCORE_THREAD_READY;
        ready_mask_add_locked(old_index);
    }
    ready_mask_remove_locked(next_index);
    next->state = XSCORE_THREAD_RUNNING;
    set_current_thread_idx(next_index);
    current_slice_ticks[current_cpu()] = 0;
    preemption_pending[current_cpu()] = 0;
    xscore_spin_unlock(&thread_lock);

    if (!xscore_vmm_activate(next->address_space_root)) {
        xscore_serial_write("[FATAL] Failed to activate thread address space\n");
        for (;;) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#elif defined(__arm__) || defined(__riscv)
            __asm__ volatile("wfi");
#else
            __asm__ volatile("");
#endif
        }
    }
    xscore_smp_note_address_space(next->address_space_root);
    xscore_tss_set_rsp0(next->privilege_stack_top);
    xscore_sched_record_switch();
#if defined(__x86_64__)
    /* Preserve the architecture's user floating-point state across a switch. */
    if (old->fpu_initialized) {
        __asm__ volatile("fxsave %0" : "=m"(old->fpu_state));
    }
    if (next->fpu_initialized) {
        __asm__ volatile("fxrstor %0" : : "m"(next->fpu_state));
    }
#elif defined(__i386__)
    /* FNSAVE/FRSTOR preserve the legacy x87 state without requiring SSE. */
    if (i386_x87_state_enabled) {
        if (old->fpu_initialized) {
            __asm__ volatile("fnsave %0" : "=m"(old->fpu_state) : : "memory");
        }
        if (next->fpu_initialized) {
            __asm__ volatile("frstor %0" : : "m"(next->fpu_state) : "memory");
        }
    }
#endif
    xscore_context_switch(&old->context, &next->context);
    xscore_irq_restore(irq_flags);
}

/* Kernel wait loops call yield/block from inside a user thread's syscall.
 * Honor exit-group requests there so a blocked sibling cannot loop forever
 * without returning to Ring 3 for the interrupt-exit trampoline. */
static void exit_current_thread_if_requested(void) {
    if (!threads_initialized) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    struct xscore_thread *thread = &threads[current_thread_index];
    int requested = thread->state == XSCORE_THREAD_RUNNING &&
                    thread->user_mode_thread && thread->exit_requested;
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    if (requested) xscore_thread_exit();
}

void xscore_thread_yield(void) {
    if (!threads_initialized) return;
    exit_current_thread_if_requested();
    reap_detached_threads();
    uint32_t next = find_next_runnable();
    switch_to(next);
}

int xscore_thread_block(void) {
    if (!threads_initialized) return 0;
    reap_detached_threads();
    uint32_t current_index = current_thread_index;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    if (threads[current_index].state != XSCORE_THREAD_RUNNING) {
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        return 0;
    }
    struct xscore_thread *current = &threads[current_index];
    /* A wake may arrive after the waiter publishes its identity but before
     * this call changes RUNNING to BLOCKED.  Consume it here rather than
     * sleeping past the event. */
    if (current->wake_pending) {
        current->wake_pending = 0;
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        return 0;
    }
    ready_mask_remove_locked(current_index);
    current->state = XSCORE_THREAD_BLOCKED;
    xscore_spin_unlock_irqrestore(&thread_lock, flags);

    for (;;) {
        uintptr_t wait_flags = xscore_irq_save();
        uint32_t next = find_next_runnable();
        if (next != current_index) {
            xscore_irq_restore(wait_flags);
            switch_to(next);
            exit_current_thread_if_requested();
            return 1;
        }

        flags = xscore_spin_lock_irqsave(&thread_lock);
        uint32_t state = current->state;
        if (state == XSCORE_THREAD_READY) {
            ready_mask_remove_locked(current_index);
            current->state = XSCORE_THREAD_RUNNING;
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            xscore_irq_restore(wait_flags);
            exit_current_thread_if_requested();
            return 1;
        }
        if (state == XSCORE_THREAD_RUNNING) {
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            xscore_irq_restore(wait_flags);
            exit_current_thread_if_requested();
            return 1;
        }
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        if (state != XSCORE_THREAD_BLOCKED) {
            xscore_irq_restore(wait_flags);
            return 0;
        }

        /* Keep a blocked caller asleep when the run queue is empty instead
         * of making libc retry the wait syscall in a CPU-burning loop. */
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("sti; hlt" ::: "memory");
#else
        xscore_device_irq_wait();
#endif
        xscore_irq_restore(wait_flags);
    }
}

int xscore_thread_wake(uint64_t thread_id) {
    if (!threads_initialized || thread_id == 0) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    uint32_t target_cpu = UINT32_MAX;
    int woke = 0;
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id != thread_id) continue;
        if (threads[index].state == XSCORE_THREAD_BLOCKED) {
            threads[index].state = XSCORE_THREAD_READY;
            ready_mask_add_locked(index);
            target_cpu = threads[index].owner_cpu;
            woke = 1;
            break;
        }
        if (threads[index].state == XSCORE_THREAD_READY ||
            threads[index].state == XSCORE_THREAD_RUNNING) {
            /* Preserve a wake that races the transition into BLOCKED. */
            threads[index].wake_pending = 1;
            target_cpu = threads[index].owner_cpu;
            woke = 1;
            break;
        }
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    if (!woke) return 0;
    if (target_cpu != current_cpu()) {
        xscore_smp_request_reschedule(target_cpu);
    }
    return 1;
}

int xscore_thread_reap(uint64_t thread_id) {
    if (!threads_initialized || thread_id == 0) return 0;
    /* Reserve the TCB before releasing its stacks.  Reaping can be initiated
     * by join(), detach(), or the scheduler's detached-thread sweep, so the
     * reservation prevents two callers from freeing the same guarded pages.
     * Stack/VMM teardown stays outside the spinlock because those paths can
     * take their own locks. */
    struct xscore_thread *thread = 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 1; index < XSCORE_MAX_THREADS; ++index) {
        struct xscore_thread *candidate = &threads[index];
        if (candidate->id != thread_id ||
            candidate->state != XSCORE_THREAD_EXITED ||
            index == current_thread_index) continue;
        candidate->state = XSCORE_THREAD_REAPING;
        thread = candidate;
        break;
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    if (!thread) return 0;

    release_thread_stack(thread);

    flags = xscore_spin_lock_irqsave(&thread_lock);
    /* The TCB cannot be reused while it is REAPING, but keep the identity
     * check as a defensive guard for future SMP/table changes. */
    if (thread->id != thread_id || thread->state != XSCORE_THREAD_REAPING) {
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        return 0;
    }
    uint32_t index = (uint32_t)(thread - threads);
    ready_mask_remove_locked(index);
    thread->id = 0;
    thread->owner_cpu = 0;
    thread->address_space_root = 0;
    thread->file_descriptors = 0;
    thread->entry = 0;
    thread->argument = 0;
    thread->process = 0;
    thread->user_entry = 0;
    thread->user_stack = 0;
    thread->user_arg0 = 0;
    thread->user_return_value = 0;
    thread->join_waiter_id = 0;
    thread->wake_pending = 0;
    thread->exit_requested = 0;
    thread->user_mode_thread = 0;
    thread->detached = 0;
    thread->user_transition = (struct xscore_user_transition){0};
    thread->fpu_initialized = 0;
    thread->state = XSCORE_THREAD_UNUSED;
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return 1;
}

int xscore_thread_reap_process_siblings(struct xscore_process *process,
                                       uint64_t leader_thread_id) {
    if (!threads_initialized || !process) return 1;
    for (;;) {
        uint64_t exited_thread_id = 0U;
        uint32_t target_cpu = UINT32_MAX;
        int waiting_for_reaper = 0;
        uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
        for (uint32_t index = 1; index < XSCORE_MAX_THREADS; ++index) {
            struct xscore_thread *thread = &threads[index];
            if (thread->id == 0U || thread->id == leader_thread_id ||
                thread->process != process || !thread->user_mode_thread ||
                thread->state == XSCORE_THREAD_UNUSED)
                continue;
            if (thread->state == XSCORE_THREAD_EXITED) {
                exited_thread_id = thread->id;
                break;
            }
            if (thread->state == XSCORE_THREAD_REAPING) {
                waiting_for_reaper = 1;
                target_cpu = thread->owner_cpu;
                break;
            }

            thread->exit_requested = 1U;
            target_cpu = thread->owner_cpu;
            if (thread->state == XSCORE_THREAD_BLOCKED) {
                thread->state = XSCORE_THREAD_READY;
                thread->wake_pending = 0U;
                ready_mask_add_locked(index);
            }
            waiting_for_reaper = 1;
            break;
        }
        xscore_spin_unlock_irqrestore(&thread_lock, flags);

        if (exited_thread_id != 0U) {
            if (!xscore_thread_reap(exited_thread_id)) return 0;
            continue;
        }
        if (!waiting_for_reaper) return 1;
        if (target_cpu != UINT32_MAX && target_cpu != current_cpu())
            xscore_smp_request_reschedule(target_cpu);
        else
            xscore_thread_request_reschedule();
        return 0;
    }
}

/* Detached user threads have no joiner to reclaim their TCB.  Sweep exited
 * detached entries at scheduler safe points so long-running services cannot
 * exhaust XSCORE_MAX_THREADS after a series of short-lived workers. */
static void reap_detached_threads(void) {
    if (!threads_initialized) return;
    for (;;) {
        uint64_t thread_id = 0;
        uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
        for (uint32_t index = 1; index < XSCORE_MAX_THREADS; ++index) {
            struct xscore_thread *thread = &threads[index];
            if (thread->state == XSCORE_THREAD_EXITED && thread->detached &&
                index != current_thread_index) {
                thread_id = thread->id;
                break;
            }
        }
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        if (!thread_id || !xscore_thread_reap(thread_id)) return;
    }
}

enum xscore_thread_state xscore_thread_state(uint64_t thread_id) {
    if (!threads_initialized || thread_id == 0) return XSCORE_THREAD_UNUSED;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id == thread_id) {
            enum xscore_thread_state state = threads[index].state;
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return state;
        }
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return XSCORE_THREAD_UNUSED;
}

void xscore_thread_exit(void) {
    if (!threads_initialized) {
        for (;;) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("hlt");
#elif defined(__aarch64__)
            __asm__ volatile("wfe");
#elif defined(__arm__) || defined(__riscv)
            __asm__ volatile("wfi");
#else
            __asm__ volatile("");
#endif
        }
    }
    uint64_t waiter_thread_id = 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    ready_mask_remove_locked(current_thread_index);
    waiter_thread_id = threads[current_thread_index].join_waiter_id;
    threads[current_thread_index].state = XSCORE_THREAD_EXITED;
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    if (waiter_thread_id) (void)xscore_thread_wake(waiter_thread_id);
    /* The stack remains live until another thread reaps this TCB. */
    for (;;) {
        uint32_t next = find_next_runnable();
        if (next != current_thread_index) {
            switch_to(next);
        }
        /* No runnable thread besides us — should not happen for the boot
         * thread, but halt to be safe. */
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
        __asm__ volatile("wfe");
#elif defined(__arm__) || defined(__riscv)
        __asm__ volatile("wfi");
#else
        __asm__ volatile("");
#endif
    }
}

uint64_t xscore_thread_current_id(void) {
    if (!threads_initialized) return 0;
    return threads[current_thread_index].id;
}

struct xscore_process *xscore_thread_current_process(void) {
    if (!threads_initialized) return 0;
    return threads[current_thread_index].process;
}

int xscore_thread_current_is_user_mode(void) {
    if (!threads_initialized) return 0;
    return threads[current_thread_index].user_mode_thread != 0;
}

int xscore_thread_current_exit_requested(void) {
    if (!threads_initialized) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    struct xscore_thread *thread = &threads[current_thread_index];
    int requested = thread->state == XSCORE_THREAD_RUNNING &&
                    thread->user_mode_thread && thread->exit_requested;
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return requested;
}

uint64_t xscore_thread_create_user(struct xscore_process *process,
                                   uint64_t entry, uint64_t stack,
                                   uint64_t user_arg0) {
    if (!process || !entry || !stack) return 0;
    uint64_t id = xscore_thread_create_in_address_space(
        0, 0, xscore_vmm_current_root());
    if (!id) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id != id) continue;
        threads[index].process = process;
        threads[index].user_entry = entry;
        threads[index].user_stack = stack;
        threads[index].user_arg0 = user_arg0;
        threads[index].user_mode_thread = 1;
        threads[index].file_descriptors = xscore_process_fd_table(process);
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        return id;
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return 0;
}

int xscore_thread_bind_process(uint64_t thread_id,
                               struct xscore_process *process) {
    if (!threads_initialized || !thread_id || !process) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id == thread_id &&
            threads[index].state != XSCORE_THREAD_UNUSED) {
            threads[index].process = process;
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return 1;
        }
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return 0;
}

void xscore_thread_record_user_return(uint64_t value) {
    if (!threads_initialized) return;
    threads[current_thread_index].user_return_value = value;
}

int64_t xscore_thread_join(uint64_t thread_id, uint64_t *return_value) {
    if (!threads_initialized || !thread_id ||
        thread_id == xscore_thread_current_id()) return -AKAOS_EINVAL;
    for (;;) {
        uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
        struct xscore_thread *target = 0;
        for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
            if (threads[index].id == thread_id) { target = &threads[index]; break; }
        }
        if (!target || target->state == XSCORE_THREAD_UNUSED ||
            !target->user_mode_thread) {
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return -AKAOS_ESRCH;
        }
        if (target->detached ||
            (target->join_waiter_id &&
             target->join_waiter_id != xscore_thread_current_id())) {
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return -AKAOS_EINVAL;
        }
        if (target->state == XSCORE_THREAD_EXITED) {
            uint64_t result = target->user_return_value;
            target->join_waiter_id = 0;
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            if (return_value) *return_value = result;
            return xscore_thread_reap(thread_id) ? 0 : -AKAOS_EAGAIN;
        }
        target->join_waiter_id = xscore_thread_current_id();
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        if (!xscore_thread_block()) return -AKAOS_EAGAIN;
    }
}

int xscore_thread_detach(uint64_t thread_id) {
    if (!threads_initialized || !thread_id ||
        thread_id == xscore_thread_current_id()) return -AKAOS_EINVAL;
    uintptr_t flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        struct xscore_thread *thread = &threads[index];
        if (thread->id != thread_id || !thread->user_mode_thread) continue;
        if (thread->join_waiter_id) {
            xscore_spin_unlock_irqrestore(&thread_lock, flags);
            return -AKAOS_EBUSY;
        }
        thread->detached = 1;
        int exited = thread->state == XSCORE_THREAD_EXITED;
        xscore_spin_unlock_irqrestore(&thread_lock, flags);
        if (exited) (void)xscore_thread_reap(thread_id);
        return 0;
    }
    xscore_spin_unlock_irqrestore(&thread_lock, flags);
    return -AKAOS_ESRCH;
}

int xscore_thread_bind_fd_table(uint64_t thread_id,
                                struct xscore_fd_table *table) {
    if (!threads_initialized || thread_id == 0 || !table) return 0;
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id == thread_id &&
            threads[index].state != XSCORE_THREAD_UNUSED) {
            threads[index].file_descriptors = table;
            return 1;
        }
    }
    return 0;
}

int xscore_thread_replace_current_address_space(uint64_t address_space_root) {
    if (!threads_initialized || !address_space_root) return 0;
    struct xscore_thread *thread = &threads[current_thread_index];
    if (thread->state != XSCORE_THREAD_RUNNING) return 0;
    xscore_vmm_address_space_sync_kernel(address_space_root);
    if (!xscore_vmm_activate(address_space_root)) return 0;
    xscore_smp_note_address_space(address_space_root);
    thread->address_space_root = address_space_root;
    return 1;
}

struct xscore_fd_table *xscore_thread_current_fd_table(void) {
    if (!threads_initialized) return 0;
    return threads[current_thread_index].file_descriptors;
}

uint64_t xscore_thread_privilege_stack_top(uint64_t thread_id) {
    if (!threads_initialized || thread_id == 0) return 0;
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id == thread_id &&
            threads[index].state != XSCORE_THREAD_UNUSED) {
            return threads[index].privilege_stack_top;
        }
    }
    return 0;
}

uint64_t xscore_thread_current_privilege_stack_top(void) {
    if (!threads_initialized) return 0;
    return threads[current_thread_index].privilege_stack_top;
}

struct xscore_user_transition *xscore_thread_current_user_transition(void) {
    if (!threads_initialized) return 0;
    return &threads[current_thread_index].user_transition;
}

void xscore_thread_set_user_tp(uint64_t user_tp) {
    struct xscore_user_transition *transition =
        xscore_thread_current_user_transition();
    if (transition) transition->user_tp = user_tp;
}

uint64_t xscore_thread_user_tp(void) {
    struct xscore_user_transition *transition =
        xscore_thread_current_user_transition();
    return transition ? transition->user_tp : 0;
}

void xscore_thread_prepare_user_transition(void) {
    struct xscore_user_transition *transition = xscore_thread_current_user_transition();
    if (!transition) return;
    transition->kernel_rsp = 0;
    transition->kernel_rip = 0;
    transition->exit_status = UINT64_MAX;
    transition->active = 0;
}

void xscore_thread_record_user_exit(uint64_t exit_status) {
    struct xscore_user_transition *transition = xscore_thread_current_user_transition();
    if (transition && transition->active) transition->exit_status = exit_status;
}

uint64_t xscore_thread_user_exit_status(void) {
    struct xscore_user_transition *transition = xscore_thread_current_user_transition();
    return transition ? transition->exit_status : UINT64_MAX;
}

void xscore_thread_schedule(void) {
    /*
     * Timer policy only latches the request here. Ring 0 code consumes it at
     * an explicit safe checkpoint; the IRQ dispatcher consumes it only when
     * the saved interrupt frame proves that execution came from Ring 3.
     */
    if (!threads_initialized ||
        xscore_thread_state(threads[current_thread_index].id) !=
            XSCORE_THREAD_RUNNING) return;
    uint32_t cpu = current_cpu();
    ++current_slice_ticks[cpu];
    if (current_slice_ticks[cpu] >= TIME_SLICE_TICKS &&
        find_next_runnable() != current_thread_index) {
        __atomic_store_n(&preemption_pending[cpu], 1U, __ATOMIC_RELEASE);
    }
}

int xscore_thread_preemption_checkpoint(void) {
    uint32_t cpu = current_cpu();
    if (!threads_initialized ||
        !__atomic_load_n(&preemption_pending[cpu], __ATOMIC_ACQUIRE) ||
        xscore_thread_state(threads[current_thread_index].id) !=
            XSCORE_THREAD_RUNNING) return 0;
    __atomic_store_n(&preemption_pending[cpu], 0U, __ATOMIC_RELEASE);
    xscore_sched_record_preemption();
    uint32_t next = find_next_runnable();
    if (next == current_thread_index) return 0;
    switch_to(next);
    return 1;
}

uint32_t xscore_thread_owner_cpu(uint64_t thread_id) {
    if (!threads_initialized || thread_id == 0) return UINT32_MAX;
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&thread_lock);
    for (uint32_t index = 0; index < XSCORE_MAX_THREADS; ++index) {
        if (threads[index].id == thread_id &&
            threads[index].state != XSCORE_THREAD_UNUSED) {
            uint32_t cpu = threads[index].owner_cpu;
            xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
            return cpu;
        }
    }
    xscore_spin_unlock_irqrestore(&thread_lock, irq_flags);
    return UINT32_MAX;
}
