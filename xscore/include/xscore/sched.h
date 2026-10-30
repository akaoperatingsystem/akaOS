#ifndef XSCORE_SCHED_H
#define XSCORE_SCHED_H

#include <stdint.h>

/*
 * CPU context saved/restored during context switches.
 * This structure defines the register layout pushed by the ISR
 * stubs in entry.S and is the foundation for preemptive scheduling.
 *
 * NOTE: Preemptive scheduling is NOT yet implemented. This header defines
 * only the state needed for future context switches.
 */
struct xscore_cpu_context {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp;
    uint64_t rdx, rcx, rbx, rax;
    /* Pushed by ISR stub: */
    uint64_t vector;
    uint64_t error_code;
    /* Pushed by CPU on interrupt: */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/*
 * Per-CPU state.
 * One record exists for every discovered logical CPU.  Non-x86 secondary
 * records currently own idle/timer accounting; user-task migration remains
 * BSP-only until their trap/address-space paths are audited.
 */
struct xscore_per_cpu {
    uint32_t apic_id;
    uint64_t kernel_stack_top;
    uint64_t timer_ticks;
    /* Future: current_thread, run queues, etc. */
};

struct xscore_sched_stats {
    uint64_t timer_ticks;        /* timer interrupts observed */
    uint64_t context_switches;
    uint64_t preemptions;        /* completed safe-checkpoint preemptions */
};

/*
 * Initialize the single BSP per-CPU record. Must be called after the
 * Local APIC is enabled (reads the APIC ID) and before the timer starts.
 */
void xscore_sched_initialize(void);
void xscore_sched_register_core(uint32_t cpu_index, uint32_t apic_id, uint64_t stack_top);

/*
 * Timer-tick hook. Called from the timer interrupt with interrupts
 * disabled. Drives the audited x86 scheduler; native ARM/RISC-V currently
 * use the hook for per-CPU accounting only.
 */
void xscore_sched_timer_tick(void);
uint64_t xscore_sched_cpu_ticks(uint32_t cpu_index);

/*
 * Record that a context switch happened. Called by the thread layer when
 * it switches between runnable threads.
 */
void xscore_sched_record_switch(void);
void xscore_sched_record_preemption(void);

/* Copy the current scheduler statistics. */
struct xscore_sched_stats xscore_sched_get_stats(void);

#endif
