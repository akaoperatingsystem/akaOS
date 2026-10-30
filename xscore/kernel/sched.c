#include <xscore/apic.h>
#include <xscore/sched.h>
#include <xscore/syscall.h>
#include <xscore/thread.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Scheduler foundation with SMP multi-core support.
 *
 * This module owns per-CPU scheduler accounting and the timer-tick handoff.
 */

static struct xscore_per_cpu per_cpu[SMP_MAX_CPUS];
static struct xscore_sched_stats sched_stats;
static xscore_spinlock_t sched_lock = XSCORE_SPINLOCK_INIT;

void xscore_sched_initialize(void) {
    uint32_t bsp_id = xscore_apic_id();
    for (uint32_t i = 0; i < SMP_MAX_CPUS; ++i) {
        per_cpu[i].apic_id = (i == 0) ? bsp_id : 0xFFFFFFFF;
        per_cpu[i].kernel_stack_top = 0;
        per_cpu[i].timer_ticks = 0;
    }
    sched_stats.timer_ticks = 0;
    sched_stats.context_switches = 0;
    sched_stats.preemptions = 0;
}

void xscore_sched_register_core(uint32_t cpu_index, uint32_t apic_id, uint64_t stack_top) {
    if (cpu_index >= SMP_MAX_CPUS) return;
    per_cpu[cpu_index].apic_id = apic_id;
    per_cpu[cpu_index].kernel_stack_top = stack_top;
    per_cpu[cpu_index].timer_ticks = 0;
}

void xscore_sched_timer_tick(void) {
    uint32_t core = xscore_smp_current_cpu_index();
    if (core < SMP_MAX_CPUS) {
        ++per_cpu[core].timer_ticks;
    }
    if (core == 0) {
        uintptr_t flags = xscore_spin_lock_irqsave(&sched_lock);
        ++sched_stats.timer_ticks;
        xscore_spin_unlock_irqrestore(&sched_lock, flags);
    }
    xscore_syscall_futex_timer_tick();
    /* x86 already has an audited context-switch path.  ARM/RISC-V now own
     * real per-CPU timer interrupts, but their user-task address-space and
     * trap-frame migration is intentionally still BSP-only.  Keep those
     * ticks as accounting until that migration boundary is complete. */
#if defined(__x86_64__) || defined(__i386__)
    xscore_thread_schedule();
#endif
}

uint64_t xscore_sched_cpu_ticks(uint32_t cpu_index) {
    if (cpu_index >= SMP_MAX_CPUS) return 0;
    return per_cpu[cpu_index].timer_ticks;
}

void xscore_sched_record_switch(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&sched_lock);
    ++sched_stats.context_switches;
    xscore_spin_unlock_irqrestore(&sched_lock, flags);
}

void xscore_sched_record_preemption(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&sched_lock);
    ++sched_stats.preemptions;
    xscore_spin_unlock_irqrestore(&sched_lock, flags);
}

struct xscore_sched_stats xscore_sched_get_stats(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&sched_lock);
    struct xscore_sched_stats stats = sched_stats;
    xscore_spin_unlock_irqrestore(&sched_lock, flags);
    return stats;
}
