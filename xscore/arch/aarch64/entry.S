.section .text.boot, "ax"
.global _start_aarch64
.type _start_aarch64, %function

_start_aarch64:
    /* PSCI enters every CPU at this address. CPU 0 owns normal boot; the
     * other CPUs receive a private stack and wait for the C release flag. */
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbnz x0, .Laarch64_secondary

    /* Setup EL1 stack pointer */
    ldr x0, =boot_stack_top
    mov sp, x0

    /* Set exception vector base */
    ldr x0, =vectors_aarch64
    msr vbar_el1, x0

    /* Enable FP/SIMD (NEON) access in EL1 and EL0 */
    mov x0, #(3 << 20)
    msr cpacr_el1, x0
    isb

    /* Clear BSS */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
1:  cmp x0, x1
    b.ge 3f
    str xzr, [x0], #8
    b 1b

3:  /* Jump into C main */
    bl xscore_aarch64_main

.Laarch64_syscall_exit:
    bl xscore_thread_current_user_transition
    cbz x0, 2f
    str xzr, [x0, #24]  /* transition->active = 0 */
    ldr x1, [x0, #0]    /* transition->kernel_rsp */
    ldr x2, [x0, #8]    /* transition->kernel_rip */
    mov sp, x1
    br x2

2:  /* Halted state */
    wfe
    b 2b

.global _start_aarch64_secondary
.type _start_aarch64_secondary, %function
_start_aarch64_secondary:
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    b .Laarch64_secondary

.Laarch64_secondary:
    mov w19, w0
    cmp w19, #4
    b.hs 2b
    ldr x1, =secondary_stacks
    lsl x2, x19, #14                 /* 16 KiB stack per CPU */
    add x1, x1, x2
    add x1, x1, #(1 << 14)
    mov sp, x1
    ldr x1, =vectors_aarch64
    msr vbar_el1, x1
    ldr x1, =xscore_secondary_release
    lsl x2, x19, #2
    add x1, x1, x2
.Laarch64_secondary_wait:
    ldr w2, [x1]
    cbnz w2, .Laarch64_secondary_run
    wfe
    b .Laarch64_secondary_wait
.Laarch64_secondary_run:
    mov w0, w19
    bl xscore_secondary_cpu_entry
    b 2b

/* --------------------------------------------------------------------------
 * AArch64 Exception Vector Table (16 entries, aligned to 2048 bytes)
 * -------------------------------------------------------------------------- */
.align 11
.global vectors_aarch64
vectors_aarch64:
    /* Current EL with SP0 */
    .align 7; b default_exception_handler
    .align 7; b device_irq_handler
    .align 7; b default_exception_handler
    .align 7; b default_exception_handler

    /* Current EL with SPx */
    .align 7; b default_exception_handler
    .align 7; b device_irq_handler
    .align 7; b default_exception_handler
    .align 7; b default_exception_handler

    /* Lower EL using AArch64 */
    .align 7; b el0_sync_handler
    .align 7; b device_irq_handler
    .align 7; b default_exception_handler
    .align 7; b default_exception_handler

    /* Lower EL using AArch32 */
    .align 7; b default_exception_handler
    .align 7; b device_irq_handler
    .align 7; b default_exception_handler
    .align 7; b default_exception_handler

.global print_hex_reg
print_hex_reg:
    mov x1, #0x09000000
    mov x2, #60
1:  lsr x3, x0, x2
    and x3, x3, #0xF
    cmp x3, #10
    blo 2f
    add x3, x3, #('A' - 10)
    b 3f
2:  add x3, x3, #'0'
3:  str w3, [x1]
    subs x2, x2, #4
    b.ge 1b
    mov w3, #'\r'
    str w3, [x1]
    mov w3, #'\n'
    str w3, [x1]
    ret

default_exception_handler:
    mov x1, #0x09000000
    mov w3, #'E'
    str w3, [x1]
    mov w3, #'S'
    str w3, [x1]
    mov w3, #'R'
    str w3, [x1]
    mov w3, #':'
    str w3, [x1]
    mrs x0, esr_el1
    bl print_hex_reg

    mov x1, #0x09000000
    mov w3, #'E'
    str w3, [x1]
    mov w3, #'L'
    str w3, [x1]
    mov w3, #'R'
    str w3, [x1]
    mov w3, #':'
    str w3, [x1]
    mrs x0, elr_el1
    bl print_hex_reg

    mov x1, #0x09000000
    mov w3, #'F'
    str w3, [x1]
    mov w3, #'A'
    str w3, [x1]
    mov w3, #'R'
    str w3, [x1]
    mov w3, #':'
    str w3, [x1]
    mrs x0, far_el1
    bl print_hex_reg

4:  wfe
    b 4b

device_irq_handler:
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #256]
    bl xscore_device_irq_dispatch
    /* Keep the per-thread EL0 TPIDR value coherent across timer-driven
       scheduling.  The full register frame is restored below. */
    bl xscore_thread_current_user_transition
    cbz x0, .Laarch64_irq_tp_ready
    ldr x1, [x0, #48]
    msr tpidr_el0, x1
.Laarch64_irq_tp_ready:
    ldp x0, x1, [sp, #256]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #272
    eret

/* Early identity-mapped stacks used while a PSCI secondary enters before it
 * has installed the normal kernel address space. */
.section .bss
.align 14
.global secondary_stacks
secondary_stacks:
    .skip (4 * (1 << 14))

.text
el0_sync_handler:
    /* Save ELR_EL1 and SPSR_EL1 first so context switches and deferred
     * preemption during blocking syscalls cannot clobber user return state. */
    sub sp, sp, #16
    mrs x9, elr_el1
    mrs x10, spsr_el1
    stp x9, x10, [sp, #0]

    /* Save full user register frame (256 bytes) */
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x9, x10, [sp, #-16]!
    stp x7, x8, [sp, #-16]!
    stp x5, x6, [sp, #-16]!
    stp x3, x4, [sp, #-16]!
    stp x1, x2, [sp, #-16]!
    str x0, [sp, #-16]!

    mrs x9, esr_el1
    lsr x9, x9, #26 /* EC field */
    cmp x9, #0x15   /* SVC in AArch64 */
    b.eq 1f

    /* Recover a bounded user-stack data abort and retry the instruction. */
    mrs x0, esr_el1
    mrs x1, far_el1
    bl xscore_aarch64_handle_user_exception
    cbnz x0, .Laarch64_el0_restore

    /* Exception in EL0: dump registers for debugging */
    mov x1, #0x09000000
    mov w3, #'['
    str w3, [x1]
    mov w3, #'E'
    str w3, [x1]
    mov w3, #'L'
    str w3, [x1]
    mov w3, #'0'
    str w3, [x1]
    mov w3, #']'
    str w3, [x1]
    mrs x0, esr_el1
    bl print_hex_reg
    mrs x0, elr_el1
    bl print_hex_reg
    mrs x0, far_el1
    bl print_hex_reg
    wfe
    b .

1:  /* Handle system call */
    /* Syscall dispatch: x8=num, x0=arg0, x1=arg1, x2=arg2, x3=arg3 */
    ldr x0, [sp, #0]    /* arg0 */
    ldr x1, [sp, #16]   /* arg1 */
    ldr x2, [sp, #24]   /* arg2 */
    ldr x3, [sp, #32]   /* arg3 */
    mov x0, x8
    mov x1, sp
    /* Snapshot the architectural return state before entering C.  The
     * capture path may allocate and lock, so reading these system registers
     * later would race a transition/context switch and could save a kernel
     * PC as the child fork resume address. */
    mrs x2, elr_el1
    mrs x3, sp_el0
    mrs x4, spsr_el1
    bl xscore_process_capture_syscall_context
    ldr x0, [sp, #0]
    ldr x1, [sp, #16]
    ldr x2, [sp, #24]
    ldr x3, [sp, #32]
    ldr x8, [sp, #72]
.Laarch64_dispatch_syscall:

    mov x4, x3
    mov x3, x2
    mov x2, x1
    mov x1, x0
    mov x0, x8
    bl xscore_syscall_dispatch_aarch64

    /* Check if return value is XSCORE_SYSCALL_EXIT_TO_KERNEL */
    mov x1, #1
    lsl x1, x1, #63
    cmp x0, x1
    b.eq .Laarch64_syscall_exit

    /* Overwrite saved x0 with return value */
    str x0, [sp, #0]

.Laarch64_el0_restore:
    /* The saved EL0 frame has no slot for TPIDR_EL0; restore it from the
       current XSCore transition record before loading user registers. */
    bl xscore_thread_current_user_transition
    cbz x0, .Laarch64_el0_tp_ready
    ldr x1, [x0, #48]
    msr tpidr_el0, x1
.Laarch64_el0_tp_ready:
    /* Restore all user registers */
    ldr x0, [sp], #16
    ldp x1, x2, [sp], #16
    ldp x3, x4, [sp], #16
    ldp x5, x6, [sp], #16
    ldp x7, x8, [sp], #16
    ldp x9, x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    /* Restore ELR_EL1 and SPSR_EL1 saved on entry */
    ldp x9, x10, [sp], #16
    msr elr_el1, x9
    msr spsr_el1, x10
    eret

2:  /* Exit to kernel continuation */
    bl xscore_thread_current_user_transition
    cbz x0, 3f
    str xzr, [x0, #24]  /* active = 0 */
    ldr x1, [x0, #0]    /* kernel_sp */
    mov sp, x1
    ldr x1, [x0, #8]    /* kernel_pc */
    br x1

3:  wfe
    b 3b

/* --------------------------------------------------------------------------
 * Context Switch: xscore_context_switch(old_ctx, new_ctx)
 * x0 = old_ctx (struct xscore_thread_context *)
 * x1 = new_ctx (struct xscore_thread_context *)
 * -------------------------------------------------------------------------- */
.global xscore_context_switch
.type xscore_context_switch, %function
xscore_context_switch:
    cbz x0, 1f
    /* Save callee-saved registers to old_ctx */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29, [x0, #80]
    str x30, [x0, #88]
    mov x2, sp
    str x2, [x0, #96]
    mrs x2, elr_el1
    str x2, [x0, #104]
    mrs x2, spsr_el1
    str x2, [x0, #112]
    mrs x2, sp_el0
    str x2, [x0, #120]

1:  /* Restore callee-saved registers from new_ctx */
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldr x29, [x1, #80]
    ldr x30, [x1, #88]
    ldr x2, [x1, #96]
    mov sp, x2
    ldr x2, [x1, #104]
    msr elr_el1, x2
    ldr x2, [x1, #112]
    msr spsr_el1, x2
    ldr x2, [x1, #120]
    msr sp_el0, x2
    isb
    ret

/* --------------------------------------------------------------------------
 * User mode enter:
 * xscore_user_enter(uint64_t entry, uint64_t sp, struct xscore_user_transition *trans)
 * x0 = user_entry
 * x1 = user_sp
 * x2 = trans
 * -------------------------------------------------------------------------- */
.global xscore_user_enter
.type xscore_user_enter, %function
xscore_user_enter:
    /*
     * EL0 is free to overwrite every general-purpose register.  Preserve the
     * kernel callee-saved set and LR on the kernel stack so this routine can
     * still satisfy the AArch64 procedure-call ABI when the process exits.
     * The exception return path restores SP to this frame and branches to the
     * local continuation below.
     */
    /* A timer IRQ between programming ELR/SP_EL0/SPSR and eret could switch
     * the current TCB and leave the CPU reporting an EL0 fault at this EL1
     * helper.  Keep the handoff atomic; SPSR_EL1 below deliberately re-enables
     * interrupts once execution reaches the new user context. */
    msr daifset, #2
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    /* Save kernel continuation to trans */
    mov x3, sp
    str x3, [x2, #0]    /* kernel_rsp */
    adr x3, 1f
    str x3, [x2, #8]    /* kernel_rip */
    mov x3, #1
    str x3, [x2, #24]   /* active = 1 */

    /* Setup ELR_EL1 and SPSR_EL1 for EL0 entry */
    msr elr_el1, x0
    msr sp_el0, x1
    ldr x3, [x2, #48]
    msr tpidr_el0, x3
    ldr x0, [x2, #40]        /* first argument for a user thread */
    mov x3, #0x00       /* EL0t mode with all DAIF interrupts unmasked */
    msr spsr_el1, x3
    isb
    eret

1:  /* Return point when user process exits */
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

.global xscore_user_resume
.type xscore_user_resume, %function
xscore_user_resume:
    /* Do not allow a timer-driven context switch to run before the saved
     * context has been copied and EL0 state is ready.  The SPSR loaded below
     * controls interrupt masking after eret. */
    msr daifset, #2
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    /* A timer interrupt may switch threads while this helper is preparing
     * EL0. Keep the context and transition in callee-saved registers so
     * switch_to() cannot restore caller-saved x0/x1/x4 as unrelated values. */
    mov x19, x0
    mov x20, x1
    /* x20 now holds the transition record throughout frame construction. */
    ldr x5, [x20, #48]
    msr tpidr_el0, x5

    mov x3, sp
    str x3, [x20, #0]
    adr x3, .Laarch64_user_resume_return
    str x3, [x20, #8]
    mov x3, #1
    str x3, [x20, #24]

    ldr x5, [x19, #256]
    msr elr_el1, x5
    ldr x5, [x19, #264]
    msr sp_el0, x5
    ldr x5, [x19, #272]
    msr spsr_el1, x5

    sub sp, sp, #256
    mov x5, sp
    mov x6, #16
.Laarch64_copy_fork_frame:
    ldp x7, x8, [x19], #16
    stp x7, x8, [x5], #16
    subs x6, x6, #1
    b.ne .Laarch64_copy_fork_frame

    ldr x0, [sp], #16
    ldp x1, x2, [sp], #16
    ldp x3, x4, [sp], #16
    ldp x5, x6, [sp], #16
    ldp x7, x8, [sp], #16
    ldp x9, x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    eret

.Laarch64_user_resume_return:
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
.size xscore_user_resume, .-xscore_user_resume

/* Boot stack */
.section .data
.align 12
boot_stack:
    .space 0x4000
boot_stack_top:
