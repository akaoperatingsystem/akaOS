.syntax unified
.cpu cortex-a7
.arm

.section .text.boot, "ax", %progbits
.global _start_arm32
.type _start_arm32, %function
_start_arm32:
    cpsid if
    /* PSCI releases secondary CPUs at the same entry. Read the ARM MPIDR
     * before sharing the primary boot stack and route non-zero cores to a
     * private early stack. */
    mrc p15, 0, r4, c0, c0, 5
    and r4, r4, #0xFF
    cmp r4, #0
    bne .Larm32_secondary
    cps #0x12
    ldr sp, =irq_stack_top
    cps #0x17                  /* Abort mode owns a dedicated exception stack. */
    ldr sp, =abort_stack_top
    cps #0x13                  /* Return to Supervisor mode for kernel boot. */
    ldr sp, =boot_stack_top

    /* Install the ARMv7 exception vector base before enabling the MMU. */
    ldr r0, =vectors_arm32
    mcr p15, 0, r0, c12, c0, 0
    isb

    /* Clear .bss while the loader's physical identity is still active. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
.Lbss_clear:
    cmp r0, r1
    bhs .Lbss_done
    str r2, [r0], #4
    b .Lbss_clear
.Lbss_done:
    bl xscore_arm32_main

.Larm32_secondary:
    cmp r4, #4
    bhs .Lhalt
    cps #0x13
    ldr r1, =secondary_stacks
    mov r2, #16384
    mul r3, r4, r2
    add sp, r1, r3
    add sp, sp, r2
    /* IRQ mode has a banked SP; give each secondary its own interrupt stack
     * before the local timer is unmasked. */
    mov r3, r4
    ldr r1, =secondary_irq_stacks
    mul r3, r3, r2
    add r1, r1, r3
    add r1, r1, r2
    cps #0x12
    mov sp, r1
    cps #0x13
    ldr r1, =secondary_stacks
    mul r3, r4, r2
    add r1, r1, r3
    add sp, r1, r2
    ldr r0, =vectors_arm32
    mcr p15, 0, r0, c12, c0, 0
    isb
    ldr r1, =xscore_secondary_release
    add r1, r1, r4, lsl #2
.Larm32_secondary_wait:
    ldr r2, [r1]
    cmp r2, #0
    bne .Larm32_secondary_run
    wfe
    b .Larm32_secondary_wait
.Larm32_secondary_run:
    mov r0, r4
    bl xscore_secondary_cpu_entry
    b .Lhalt

.Lhalt:
    wfi
    b .Lhalt
.size _start_arm32, .-_start_arm32

.text
.global _start_arm32_secondary
.type _start_arm32_secondary, %function
_start_arm32_secondary:
    cpsid if
    mrc p15, 0, r4, c0, c0, 5
    and r4, r4, #0xFF
    b .Larm32_secondary
.size _start_arm32_secondary, .-_start_arm32_secondary

/* Identity-mapped early stacks used before a secondary installs the kernel
 * MMU context. The first slot is unused (the BSP has boot_stack). */
.section .bss
.align 14
.global secondary_stacks
secondary_stacks:
    .skip (4 * 16384)

.align 12
.global secondary_irq_stacks
secondary_irq_stacks:
    .skip (4 * 16384)

.text
/* ARMv7 vectors are eight words at a 32-byte-aligned address. */
.align 5
.global vectors_arm32
vectors_arm32:
    b _start_arm32
    b arm32_undef_exception
    b arm32_svc_vector
    b arm32_pabt_exception
    b arm32_dabt_exception
    b arm32_default_exception
    b arm32_irq_exception
    b arm32_default_exception

.text
.align 2

.global xscore_arm32_load_ttbr
.type xscore_arm32_load_ttbr, %function
xscore_arm32_load_ttbr:
    mcr p15, 0, r0, c2, c0, 0
    dsb sy
    isb
    bx lr
.size xscore_arm32_load_ttbr, .-xscore_arm32_load_ttbr

.global xscore_arm32_enable_mmu
.type xscore_arm32_enable_mmu, %function
xscore_arm32_enable_mmu:
    /* TTBCR: N = 0 (TTBR0 covers all 4 GB) */
    mov r0, #0
    mcr p15, 0, r0, c2, c0, 2
    /* DACR: Client mode for all 16 domains */
    ldr r0, =0x55555555
    mcr p15, 0, r0, c3, c0, 0
    mrc p15, 0, r0, c1, c0, 0
    /* Use the classic AP[2:0] permission model. With AFE set, AP[0]
     * becomes the access flag and our AP=011 user pages would fault. */
    bic r0, r0, #(1 << 29)  /* SCTLR.AFE */
    bic r0, r0, #(1 << 28)  /* SCTLR.TRE */
    orr r0, r0, #1          /* SCTLR.M: MMU */
    orr r0, r0, #4          /* SCTLR.C: data cache */
    orr r0, r0, #0x1000     /* SCTLR.I: instruction cache */
    mcr p15, 0, r0, c1, c0, 0
    dsb sy
    isb
    bx lr
.size xscore_arm32_enable_mmu, .-xscore_arm32_enable_mmu

.global xscore_arm32_invalidate_tlb
.type xscore_arm32_invalidate_tlb, %function
xscore_arm32_invalidate_tlb:
    mov r0, #0
    mcr p15, 0, r0, c8, c7, 0   /* Invalidate entire unified TLB */
    mcr p15, 0, r0, c7, c5, 0   /* Invalidate entire Instruction Cache (ICIALLU) */
    mcr p15, 0, r0, c7, c5, 6   /* Invalidate entire Branch Predictor (BPIALL) */
    dsb sy
    isb
    bx lr
.size xscore_arm32_invalidate_tlb, .-xscore_arm32_invalidate_tlb

.global xscore_arm32_invalidate_page
.type xscore_arm32_invalidate_page, %function
xscore_arm32_invalidate_page:
    mcr p15, 0, r0, c8, c7, 1
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    dsb sy
    isb
    bx lr
.size xscore_arm32_invalidate_page, .-xscore_arm32_invalidate_page

/* Cooperative context switch; struct xscore_thread_context offsets are
 * r4-r11 (0..28), lr (32), and sp (36). */
.global xscore_context_switch
.type xscore_context_switch, %function
xscore_context_switch:
    cmp r0, #0
    beq .Lswitch_restore
    stmia r0, {r4-r11, lr}
    str sp, [r0, #36]
.Lswitch_restore:
    ldmia r1, {r4-r11, lr}
    ldr sp, [r1, #36]
    bx lr
.size xscore_context_switch, .-xscore_context_switch

/* Enter user mode and remember the kernel continuation in the thread TCB.
 * AAPCS passes the two uint64_t values as r0/r1 and r2/r3; the transition
 * pointer is the first stack argument. */
.global xscore_user_enter
.type xscore_user_enter, %function
xscore_user_enter:
    /* Mask IRQs while the SVC return frame and banked user stack are built;
     * the CPSR written to SPSR below intentionally unmasks them in User mode. */
    cpsid i
    ldr r12, [sp]
    stmdb sp!, {r4-r11, lr}
    sub sp, sp, #4             /* preserve 8-byte AAPCS alignment */

    mov r3, sp
    str r3, [r12, #0]          /* transition->kernel_rsp */
    mov r3, #0
    str r3, [r12, #4]
    ldr r3, =.Luser_resumed
    str r3, [r12, #8]          /* transition->kernel_rip */
    str r3, [r12, #12]
    mov r3, #1
    str r3, [r12, #24]         /* transition->active */
    str r3, [r12, #28]

    /* Install the allocated stack in the bank shared by User/System mode
     * before entering the user entry point.  The ELF CRT also receives the
     * value in r2 for compatibility, but pthread-created user threads enter a
     * C function directly (without the CRT), so relying on _start to move SP
     * would leave them running on the creator's stack and corrupt its frame. */
    cps #0x1f                  /* System mode: SP is the User-mode stack. */
    mov sp, r2
    cps #0x13                  /* Return to SVC mode; its stack is untouched. */
    str r2, [sp, #0]
    str r0, [sp, #4]
    ldr r0, [r12, #48]
    mcr p15, 0, r0, c13, c0, 3 /* TPIDRURW for compiler TLS sequences */
    ldr r0, [r12, #40]        /* first argument for a user thread */
    mov r3, #0x10              /* USR mode, IRQ/FIQ unmasked, ARM state */
    msr spsr_cxsf, r3
    ldm sp, {r2, pc}^

.Luser_resumed:
    add sp, sp, #4
    ldmia sp!, {r4-r11, lr}
    bx lr
.size xscore_user_enter, .-xscore_user_enter

/* SVC #0 syscall entry. The saved frame owns the interrupted user registers,
 * return CPSR, and LR, so a blocked wait can safely switch to another user
 * thread and restore this frame later. */
.align 2
arm32_svc_vector:
    mrs r12, spsr
    stmdb sp!, {r0-r12, lr}
    str r12, [sp, #-4]!

    /* User SP is banked in User/System mode. Preserve this task's value
     * before a blocking syscall can switch to another user process. */
    bl xscore_thread_current_user_transition
    add r1, r0, #32
    cps #0x1f                  /* Switch to System mode (accesses user SP) */
    mov r2, sp
    cps #0x13                  /* Switch back to SVC mode */
    str r2, [r1]

    ldr r0, [sp, #32]          /* syscall number from user r7 */
    ldr r1, [sp, #4]           /* arg0 */
    mov r1, sp
    bl xscore_process_capture_syscall_context
    ldr r0, [sp, #32]
    ldr r1, [sp, #4]
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
.Larm32_dispatch_syscall:
    bl xscore_syscall_dispatch_arm32

    ldr r1, =0x7fffffff
    cmp r0, r1
    beq .Larm32_syscall_exit

    str r0, [sp, #4]
    /* The C dispatcher is free to clobber r2.  Reload the banked user/system
     * stack saved in the transition before restoring the interrupted frame;
     * unlike the old TP path, do not touch the user's r9 register here. */
    bl xscore_thread_current_user_transition
    cmp r0, #0
    beq .Larm32_no_user_stack
    ldr r2, [r0, #32]
    ldr r3, [r0, #48]
    mcr p15, 0, r3, c13, c0, 3
.Larm32_no_user_stack:
    cps #0x1f                  /* Switch to System mode */
    mov sp, r2
    cps #0x13                  /* Switch back to SVC mode */
    ldr r12, [sp], #4
    msr spsr_cxsf, r12
    ldmia sp!, {r0-r12, lr}
    movs pc, lr

.Larm32_syscall_exit:
    add sp, sp, #4
    bl xscore_thread_current_user_transition
    cmp r0, #0
    beq .Larm32_syscall_halt
    mov r1, #0
    str r1, [r0, #24]
    ldr sp, [r0, #0]
    ldr r1, [r0, #8]
    bx r1

.Larm32_syscall_halt:
    cpsid i
.Larm32_syscall_halt_loop:
    wfi
    b .Larm32_syscall_halt_loop

.global xscore_user_resume
.type xscore_user_resume, %function
xscore_user_resume:
    /* Mask IRQs while constructing the SVC return frame and switching the
     * banked user stack.  The SPSR copied below restores user interrupt state
     * when movs pc, lr enters the resumed context. */
    cpsid i
    mov r2, r0                  /* captured context */
    mov r3, r1                  /* transition */
    mov r12, r3                 /* retain transition while copying frame */
    stmdb sp!, {r4-r11, lr}
    sub sp, sp, #4

    mov r0, sp
    str r0, [r3, #0]
    mov r0, #0
    str r0, [r3, #4]
    ldr r0, =.Larm32_user_resume_return
    str r0, [r3, #8]
    mov r0, #0
    str r0, [r3, #12]
    mov r0, #1
    str r0, [r3, #24]
    mov r0, #0
    str r0, [r3, #28]

    ldr r0, [r2, #60]
    str r0, [r3, #32]
    cps #0x1f
    mov sp, r0
    cps #0x13

    sub sp, sp, #60
    mov r0, sp
    mov r1, #15
.Larm32_copy_fork_frame:
    ldr r3, [r2], #4
    str r3, [r0], #4
    subs r1, r1, #1
    bne .Larm32_copy_fork_frame

    ldr r3, [r12, #48]
    mcr p15, 0, r3, c13, c0, 3
    ldr r12, [sp], #4
    msr spsr_cxsf, r12
    ldmia sp!, {r0-r12, lr}
    movs pc, lr

.Larm32_user_resume_return:
    add sp, sp, #4
    ldmia sp!, {r4-r11, lr}
    bx lr
.size xscore_user_resume, .-xscore_user_resume

arm32_undef_exception:
    ldr r0, =0x09000000
    mov r1, #'U'; str r1, [r0]
    mov r1, #'N'; str r1, [r0]
    mov r1, #'D'; str r1, [r0]
    mov r1, #'\n'; str r1, [r0]
    b .Larm32_exception_halt

arm32_pabt_exception:
    stmdb sp!, {r0-r12, lr}
    mrs r3, spsr
    and r2, r3, #0x1f
    cmp r2, #0x10
    bne .Larm32_prefetch_abort_stacked_fatal

    sub sp, sp, #8
    str r3, [sp]
    mrc p15, 0, r0, c5, c0, 1   /* IFSR */
    mrc p15, 0, r1, c6, c0, 2   /* IFAR */
    bl xscore_arm32_handle_prefetch_abort
    cmp r0, #0
    beq .Larm32_prefetch_abort_saved_fatal
    ldr r3, [sp]
    add sp, sp, #8
    msr spsr_cxsf, r3
    ldmia sp!, {r0-r12, lr}
    subs pc, lr, #4              /* Retry the faulting ARM instruction. */

.Larm32_prefetch_abort_saved_fatal:
    ldr r3, [sp]
    add sp, sp, #8
    msr spsr_cxsf, r3
    ldmia sp!, {r0-r12, lr}
    b .Larm32_prefetch_abort_fatal
.Larm32_prefetch_abort_stacked_fatal:
    ldmia sp!, {r0-r12, lr}
.Larm32_prefetch_abort_fatal:
    ldr r0, =0x09000000
    mov r1, #'P'; str r1, [r0]
    mov r1, #'A'; str r1, [r0]
    mov r1, #'B'; str r1, [r0]
    mov r1, #'T'; str r1, [r0]
    mov r1, #' '; str r1, [r0]
    mrc p15, 0, r2, c6, c0, 2   /* IFAR */
    mov r3, #28
1:  lsr r1, r2, r3
    and r1, r1, #0xf
    cmp r1, #10
    addlt r1, r1, #'0'
    addge r1, r1, #('a' - 10)
    str r1, [r0]
    subs r3, r3, #4
    bge 1b

    mov r1, #' '; str r1, [r0]
    mov r1, #'F'; str r1, [r0]
    mov r1, #'S'; str r1, [r0]
    mov r1, #'R'; str r1, [r0]
    mov r1, #'='; str r1, [r0]
    mrc p15, 0, r2, c5, c0, 1   /* IFSR */
    mov r3, #28
2:  lsr r1, r2, r3
    and r1, r1, #0xf
    cmp r1, #10
    addlt r1, r1, #'0'
    addge r1, r1, #('a' - 10)
    str r1, [r0]
    subs r3, r3, #4
    bge 2b

    mov r1, #'\n'; str r1, [r0]
    b .Larm32_exception_halt

arm32_dabt_exception:
    stmdb sp!, {r0-r12, lr}
    mrs r3, spsr
    and r2, r3, #0x1f
    cmp r2, #0x10              /* Recover only faults taken from User mode. */
    bne .Larm32_data_abort_stacked_fatal

    sub sp, sp, #8
    str r3, [sp]
    mrc p15, 0, r0, c5, c0, 0 /* DFSR */
    mrc p15, 0, r1, c6, c0, 0 /* DFAR */
    bl xscore_arm32_handle_data_abort
    cmp r0, #0
    beq .Larm32_data_abort_saved_fatal
    ldr r3, [sp]
    add sp, sp, #8
    msr spsr_cxsf, r3
    ldmia sp!, {r0-r12, lr}
    subs pc, lr, #8            /* Retry the interrupted ARM instruction. */

.Larm32_data_abort_saved_fatal:
    ldr r3, [sp]
    add sp, sp, #8
    msr spsr_cxsf, r3
    ldmia sp!, {r0-r12, lr}
    b .Larm32_data_abort_fatal
.Larm32_data_abort_stacked_fatal:
    ldmia sp!, {r0-r12, lr}
.Larm32_data_abort_fatal:
    ldr r0, =0x09000000
    mov r1, #'D'; str r1, [r0]
    mov r1, #'A'; str r1, [r0]
    mov r1, #'B'; str r1, [r0]
    mov r1, #'T'; str r1, [r0]
    mov r1, #' '; str r1, [r0]
    mrc p15, 0, r2, c6, c0, 0 /* DFAR */
    mov r3, #28
1:  lsr r1, r2, r3
    and r1, r1, #0xf
    cmp r1, #10
    addlt r1, r1, #'0'
    addge r1, r1, #('a' - 10)
    str r1, [r0]
    subs r3, r3, #4
    bge 1b
    mov r1, #' '; str r1, [r0]
    mov r2, lr
    mov r3, #28
2:  lsr r1, r2, r3
    and r1, r1, #0xf
    cmp r1, #10
    addlt r1, r1, #'0'
    addge r1, r1, #('a' - 10)
    str r1, [r0]
    subs r3, r3, #4
    bge 2b
    mov r1, #'\n'; str r1, [r0]
    b .Larm32_exception_halt

arm32_irq_exception:
    stmdb sp!, {r0-r12, lr}
    mrs r0, spsr
    push {r0, r1}
    bl xscore_device_irq_dispatch
    bl xscore_thread_current_user_transition
    cmp r0, #0
    beq .Larm32_irq_no_user_tp
    ldr r3, [r0, #48]
    mcr p15, 0, r3, c13, c0, 3
.Larm32_irq_no_user_tp:
    pop {r0, r1}
    msr spsr_cxsf, r0
    ldmia sp!, {r0-r12, lr}
    subs pc, lr, #4

arm32_default_exception:
    ldr r0, =0x09000000
    mov r1, #'E'; str r1, [r0]
    mov r1, #'X'; str r1, [r0]
    mov r1, #'C'; str r1, [r0]
    mov r1, #'\n'; str r1, [r0]
.Larm32_exception_halt:
    wfi
    b .Larm32_exception_halt

.section .bss
.align 12
boot_stack_arm32:
    .space 0x4000
boot_stack_top:

.align 12
abort_stack_arm32:
    .space 0x1000
abort_stack_top:
.align 4
irq_stack:
    .space 8192
irq_stack_top:
