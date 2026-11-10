#ifndef XSCORE_THREAD_H
#define XSCORE_THREAD_H

#include <stddef.h>
#include <stdint.h>

struct xscore_fd_table;
struct xscore_process;

/*
 * XSCore kernel threads (Phase 3 — Multitasking).
 *
 * A kernel thread is the unit of execution the scheduler switches between.
 * Each thread owns a dedicated kernel stack (with a guard page) and a saved
 * register context. User-mode processes are scheduler-owned threads with a
 * private address-space root and privilege-entry stack.
 *
 * Ring 0 scheduling is cooperative at yields and safe checkpoints. Ring 3
 * tasks may be preempted at the timer interrupt-return boundary.
 */

#if defined(XSCORE_LOW_MEMORY)
#define XSCORE_MAX_THREADS 16U
#else
#define XSCORE_MAX_THREADS 64U
#endif
_Static_assert(XSCORE_MAX_THREADS <= 64U,
               "thread ready bitmap supports at most 64 TCBs");
#if defined(XSCORE_LOW_MEMORY)
#define XSCORE_THREAD_STACK_PAGES 2
#define XSCORE_THREAD_PRIVILEGE_STACK_PAGES 2
#else
#define XSCORE_THREAD_STACK_PAGES 4
#define XSCORE_THREAD_PRIVILEGE_STACK_PAGES 2
#endif

/* Thread states. */
enum xscore_thread_state {
    XSCORE_THREAD_UNUSED = 0,
    XSCORE_THREAD_READY,
    XSCORE_THREAD_RUNNING,
    XSCORE_THREAD_BLOCKED,
    XSCORE_THREAD_EXITED,
    /* A TCB reserved by one creator while its guarded stacks are built. */
    XSCORE_THREAD_ALLOCATING,
    /* An exited TCB whose kernel stacks are being released by a reaper. */
    XSCORE_THREAD_REAPING
};

/*
 * Saved register context for a thread that is not currently running.
 *
 * The context switch routine (xscore_context_switch, in entry.S) pops the
 * callee-saved registers from this structure and updates RSP. The layout
 * below must match the assembly exactly (offsets are order-sensitive).
 *
 *   rsp   — the kernel stack pointer for this thread
 *   rip   — where to resume (a function entry or a return point)
 *   rflags — saved RFLAGS (interrupts enabled for runnable threads)
 *   rbp   — saved frame pointer
 *   rbx, r12..r15 — callee-saved GPRs
 */
struct xscore_thread_context {
#if defined(__i386__)
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eflags;
    uint32_t eip;
    uint32_t esp;
#elif defined(__aarch64__)
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
    /* Exception-return state is CPU-global and must follow the thread when
     * a user task blocks inside an EL0 syscall. */
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t sp_el0;
#elif defined(__arm__)
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t lr;
    uint32_t sp;
#elif defined(__riscv)
#if __riscv_xlen == 64
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t ra;
    uint64_t sp;
    uint64_t sepc;
    uint64_t sstatus;
    uint64_t sscratch;
#else
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t ra;
    uint32_t sp;
    uint32_t sepc;
    uint32_t sstatus;
    uint32_t sscratch;
#endif
#else
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rflags;
    uint64_t rip;
    uint64_t rsp;
#endif
};

/*
 * Keep the C layout locked to the hand-written context-switch assembly.
 * These checks deliberately cover every supported target so a new port
 * cannot silently restore the wrong register or privilege-return slot.
 */
#if defined(__i386__)
_Static_assert(sizeof(struct xscore_thread_context) == 28,
               "i386 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, eflags) == 16,
               "i386 eflags offset changed");
_Static_assert(offsetof(struct xscore_thread_context, eip) == 20,
               "i386 eip offset changed");
_Static_assert(offsetof(struct xscore_thread_context, esp) == 24,
               "i386 esp offset changed");
#elif defined(__x86_64__)
_Static_assert(sizeof(struct xscore_thread_context) == 72,
               "x86_64 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, rflags) == 48,
               "x86_64 rflags offset changed");
_Static_assert(offsetof(struct xscore_thread_context, rip) == 56,
               "x86_64 rip offset changed");
_Static_assert(offsetof(struct xscore_thread_context, rsp) == 64,
               "x86_64 rsp offset changed");
#elif defined(__aarch64__)
_Static_assert(sizeof(struct xscore_thread_context) == 128,
               "AArch64 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, sp) == 96,
               "AArch64 sp offset changed");
_Static_assert(offsetof(struct xscore_thread_context, elr_el1) == 104,
               "AArch64 elr offset changed");
_Static_assert(offsetof(struct xscore_thread_context, spsr_el1) == 112,
               "AArch64 spsr offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sp_el0) == 120,
               "AArch64 sp_el0 offset changed");
#elif defined(__arm__)
_Static_assert(sizeof(struct xscore_thread_context) == 40,
               "ARM32 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, lr) == 32,
               "ARM32 lr offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sp) == 36,
               "ARM32 sp offset changed");
#elif defined(__riscv) && __riscv_xlen == 64
_Static_assert(sizeof(struct xscore_thread_context) == 136,
               "RISC-V64 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, ra) == 96,
               "RISC-V64 ra offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sp) == 104,
               "RISC-V64 sp offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sepc) == 112,
               "RISC-V64 sepc offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sstatus) == 120,
               "RISC-V64 sstatus offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sscratch) == 128,
               "RISC-V64 sscratch offset changed");
#elif defined(__riscv)
_Static_assert(sizeof(struct xscore_thread_context) == 68,
               "RISC-V32 thread context size changed");
_Static_assert(offsetof(struct xscore_thread_context, ra) == 48,
               "RISC-V32 ra offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sp) == 52,
               "RISC-V32 sp offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sepc) == 56,
               "RISC-V32 sepc offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sstatus) == 60,
               "RISC-V32 sstatus offset changed");
_Static_assert(offsetof(struct xscore_thread_context, sscratch) == 64,
               "RISC-V32 sscratch offset changed");
#endif

/* Kernel continuation and exit result for one Ring 3 interval. */
struct xscore_user_transition {
    uint64_t kernel_rsp;
    uint64_t kernel_rip;
    uint64_t exit_status;
    uint64_t active;
    /* ARM32 has one banked user SP per CPU, so save it across task switches. */
    uint64_t user_rsp;
    /* Initial argument for a newly-created user thread.  These fields are
     * appended so the architecture entry paths keep their existing offsets. */
    uint64_t user_arg0;
    /* Architecture TLS/thread-pointer value restored before returning to
     * user mode.  The native libc uses this for static TLS as well as the
     * POSIX pthread ABI. */
    uint64_t user_tp;
    /* RISC-V uses tp for the hart ID in supervisor mode, but user mode owns
     * tp as its TLS register. Keep the kernel value with the transition so a
     * user trap can restore it before calling interrupt/syscall C code. */
    uint64_t kernel_hart_id;
    /* Scratch slots used only while a RISC-V user trap swaps from the
     * transition record in sscratch to that thread's kernel stack. */
    uint64_t trap_t0;
    uint64_t trap_t1;
    uint64_t trap_tp;
    uint64_t trap_user_sp;
    uint64_t trap_t2;
};

_Static_assert(offsetof(struct xscore_user_transition, kernel_rsp) == 0,
               "user transition kernel_rsp offset changed");
_Static_assert(offsetof(struct xscore_user_transition, kernel_rip) == 8,
               "user transition kernel_rip offset changed");
_Static_assert(offsetof(struct xscore_user_transition, active) == 24,
               "user transition active offset changed");
_Static_assert(offsetof(struct xscore_user_transition, user_rsp) == 32,
               "user transition user_rsp offset changed");
_Static_assert(offsetof(struct xscore_user_transition, user_arg0) == 40,
               "user transition user_arg0 offset changed");
_Static_assert(offsetof(struct xscore_user_transition, user_tp) == 48,
               "user transition user_tp offset changed");
_Static_assert(offsetof(struct xscore_user_transition, kernel_hart_id) == 56,
               "user transition kernel_hart_id offset changed");
_Static_assert(offsetof(struct xscore_user_transition, trap_t0) == 64,
               "user transition RISC-V trap_t0 offset changed");
_Static_assert(offsetof(struct xscore_user_transition, trap_t1) == 72,
               "user transition RISC-V trap_t1 offset changed");
_Static_assert(offsetof(struct xscore_user_transition, trap_tp) == 80,
               "user transition RISC-V trap_tp offset changed");
_Static_assert(offsetof(struct xscore_user_transition, trap_user_sp) == 88,
               "user transition RISC-V trap_user_sp offset changed");
_Static_assert(offsetof(struct xscore_user_transition, trap_t2) == 96,
               "user transition RISC-V trap_t2 offset changed");
_Static_assert(sizeof(struct xscore_user_transition) == 104,
               "user transition size changed");

/*
 * Thread Control Block.
 */
struct xscore_thread {
    uint64_t id;                         /* monotonically increasing TID */
    uint32_t owner_cpu;                  /* CPU whose run queue owns it    */
    enum xscore_thread_state state;
    struct xscore_thread_context context;
    uint64_t stack_top;                  /* top of the kernel stack (highest addr) */
    uint64_t stack_base;                 /* base of the stack pages (low addr)  */
    uint64_t privilege_stack_top;        /* task-owned TSS.RSP0 stack top        */
    uint64_t privilege_stack_base;       /* guarded privilege-entry stack base   */
    uint64_t address_space_root;         /* CR3 used while this thread runs       */
    struct xscore_fd_table *file_descriptors; /* owning process descriptor table */
    struct xscore_user_transition user_transition;
    /* x86 FXSAVE-compatible state; unused by non-x86 ports. */
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    uint8_t fpu_initialized;
    void (*entry)(void *argument);       /* thread function                       */
    void *argument;                      /* argument passed to entry             */
struct xscore_process *process;      /* owning process (shared by user threads) */
    uint64_t user_entry;                 /* EL0 entry for a pthread-style thread */
    uint64_t user_stack;                 /* EL0 stack pointer                     */
    uint64_t user_arg0;                  /* first EL0 argument                    */
    uint64_t user_return_value;          /* pthread return value                  */
    uint64_t join_waiter_id;             /* one joiner, POSIX semantics            */
    uint32_t user_mode_thread;
    uint32_t detached;
    uint32_t wake_pending;                /* wake racing the RUNNING->BLOCKED edge */
    uint32_t exit_requested;              /* process-wide exit-group teardown */
};

/*
 * Initialize the thread subsystem. Allocates the thread table and prepares
 * the boot thread (the currently-running context becomes thread 0).
 * Must be called after stacks, the heap, and the scheduler foundation exist.
 * Returns 1 on success, 0 on failure.
 */
int xscore_thread_initialize(void);

/*
 * Create a new kernel thread in XSCore's kernel address space.
 *   entry:   function the thread starts executing
 *   argument: opaque pointer passed to entry
 * Returns the new thread id (> 0), or 0 on failure.
 */
uint64_t xscore_thread_create(void (*entry)(void *argument), void *argument);

/* Create a schedulable task whose context switches also activate the supplied
 * page-table root. This is the process/thread integration boundary. */
uint64_t xscore_thread_create_in_address_space(void (*entry)(void *argument),
                                               void *argument,
                                               uint64_t address_space_root);

/* Create a thread on a specific online CPU. The target CPU must already have
 * registered its idle scheduler context. */
uint64_t xscore_thread_create_on_cpu(void (*entry)(void *argument),
                                     void *argument,
                                     uint64_t address_space_root,
                                     uint32_t cpu_index);

/* Create a user-mode thread sharing the caller's address space and process
 * descriptor table.  The entry receives user_arg0 in the native first
 * argument register (or stack slot on i386). */
uint64_t xscore_thread_create_user(struct xscore_process *process,
                                   uint64_t entry, uint64_t stack,
                                   uint64_t user_arg0);
int xscore_thread_bind_process(uint64_t thread_id,
                               struct xscore_process *process);
int64_t xscore_thread_join(uint64_t thread_id, uint64_t *return_value);
int xscore_thread_detach(uint64_t thread_id);
void xscore_thread_record_user_return(uint64_t value);

/* Register an AP's current execution context as its scheduler idle thread. */
int xscore_thread_register_cpu(uint32_t cpu_index, uint64_t privilege_stack_top);

/* Return the owning CPU for a live thread, or UINT32_MAX if unknown. */
uint32_t xscore_thread_owner_cpu(uint64_t thread_id);

/*
 * Yield the CPU to the next runnable thread. Returns to the caller when
 * this thread is scheduled again. Safe to call from thread context with
 * interrupts disabled (they are re-enabled on return).
 */
void xscore_thread_yield(void);
int xscore_thread_block(void);
int xscore_thread_wake(uint64_t thread_id);
int xscore_thread_reap(uint64_t thread_id);
/* Request and reap user threads that share a process after its leader exits. */
int xscore_thread_reap_process_siblings(struct xscore_process *process,
                                       uint64_t leader_thread_id);
void xscore_thread_request_process_exit(struct xscore_process *process);
enum xscore_thread_state xscore_thread_state(uint64_t thread_id);

/*
 * Exit the current thread. The thread is marked exited and never returns;
 * the scheduler switches to the next runnable thread.
 */
void xscore_thread_exit(void);

/*
 * Return the id of the currently running thread.
 */
uint64_t xscore_thread_current_id(void);
struct xscore_process *xscore_thread_current_process(void);
int xscore_thread_current_is_user_mode(void);
int xscore_thread_current_exit_requested(void);
int xscore_thread_bind_fd_table(uint64_t thread_id,
                                struct xscore_fd_table *table);
int xscore_thread_replace_current_address_space(uint64_t address_space_root);
struct xscore_fd_table *xscore_thread_current_fd_table(void);
uint64_t xscore_thread_privilege_stack_top(uint64_t thread_id);
uint64_t xscore_thread_current_privilege_stack_top(void);
struct xscore_user_transition *xscore_thread_current_user_transition(void);
void xscore_thread_set_user_tp(uint64_t user_tp);
uint64_t xscore_thread_user_tp(void);
void xscore_thread_prepare_user_transition(void);
void xscore_thread_record_user_exit(uint64_t exit_status);
uint64_t xscore_thread_user_exit_status(void);

/*
 * Called by the scheduler / timer interrupt to perform a context switch
 * if there is another runnable thread. No-op if only one thread exists.
 */
void xscore_thread_schedule(void);
int xscore_thread_preemption_checkpoint(void);
/* Mark the current CPU for an immediate scheduler pass (IPI target). */
void xscore_thread_request_reschedule(void);

#endif
