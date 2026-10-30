.section .text.boot, "ax", @progbits
.global _start
.type _start, @function
_start:
    /* Preserve OpenSBI's boot-hart ID in the per-hart thread pointer. */
    mv tp, a0
    /* Disable interrupts, enable SUM (bit 18) */
    csrr t0, sstatus
    li t1, ~(1 << 1)
    and t0, t0, t1
    li t1, (1 << 18)
    or t0, t0, t1
    csrw sstatus, t0

    /* Setup initial boot stack */
    la sp, boot_stack_top

    /* Set trap vector */
    la t0, trap_vector
    csrw stvec, t0

    /* Clear BSS */
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    /* Call C kernel entry */
    call xscore_riscv32_main

.Lhalt:
    wfi
    j .Lhalt
.size _start, .-_start

.section .bss
.align 14
.global secondary_stacks
secondary_stacks:
    .skip (4 * 16384)

.text
.align 2
.global _start_secondary
.type _start_secondary, @function
_start_secondary:
    /* SBI HSM starts this symbol directly; the first payload entry may be
     * any boot hart, so primary/secondary identity is not inferred from a0. */
    mv t3, a0
    mv tp, t3
.Lriscv32_secondary:
    li t1, 4
    bgeu t3, t1, .Lhalt
    la t0, secondary_stacks
    li t1, 16384
    mul t2, t3, t1
    add sp, t0, t2
    add sp, sp, t1
    la t0, trap_vector
    csrw stvec, t0
    la t0, xscore_secondary_release
    slli t1, t3, 2
    add t0, t0, t1
.Lriscv32_secondary_wait:
    lw t1, 0(t0)
    bnez t1, .Lriscv32_secondary_run
    nop
    j .Lriscv32_secondary_wait
.Lriscv32_secondary_run:
    mv a0, t3
    call xscore_secondary_cpu_entry
    j .Lhalt
.size _start_secondary, .-_start_secondary

.align 4
.global trap_vector
.type trap_vector, @function
trap_vector:
    /* Check if trap came from user mode or kernel mode */
    csrrw sp, sscratch, sp
    bnez sp, .Ltrap_from_user

.Ltrap_from_kernel:
    csrrw sp, sscratch, sp      /* Restore kernel sp */
    addi sp, sp, -128
    sw ra, 0(sp)
    sw t0, 4(sp)
    sw t1, 8(sp)
    sw t2, 12(sp)
    sw a0, 24(sp)
    sw a1, 28(sp)
    sw a2, 32(sp)
    sw a3, 36(sp)
    sw a4, 40(sp)
    sw a5, 44(sp)
    sw a6, 48(sp)
    sw a7, 52(sp)
    sw t3, 96(sp)
    sw t4, 100(sp)
    sw t5, 104(sp)
    sw t6, 108(sp)
    csrr t0, sepc
    sw t0, 116(sp)
    csrr t1, sstatus
    sw t1, 120(sp)
    csrr t2, scause
    sw t2, 124(sp)

    /* Check if exception is fatal (bit 31 clear = exception, bit 31 set = interrupt) */
    srli t0, t2, 31
    beqz t0, .Lkernel_exception
    call xscore_device_irq_dispatch
    j .Lkernel_trap_return
.Lkernel_exception:

    /* Fatal kernel exception: call C diagnostic handler */
    lw a0, 124(sp) /* scause */
    lw a1, 116(sp) /* sepc */
    csrr a2, stval /* stval */
    lw a3, 120(sp) /* sstatus */
    call xscore_trap_handler_c
1:  wfi
    j 1b

.Lkernel_trap_return:
    lw t0, 116(sp)
    csrw sepc, t0
    lw t1, 120(sp)
    csrw sstatus, t1
    lw ra, 0(sp)
    lw t0, 4(sp)
    lw t1, 8(sp)
    lw t2, 12(sp)
    lw a0, 24(sp)
    lw a1, 28(sp)
    lw a2, 32(sp)
    lw a3, 36(sp)
    lw a4, 40(sp)
    lw a5, 44(sp)
    lw a6, 48(sp)
    lw a7, 52(sp)
    lw t3, 96(sp)
    lw t4, 100(sp)
    lw t5, 104(sp)
    lw t6, 108(sp)
    addi sp, sp, 128
    sret

.Ltrap_from_user:
    /* sscratch points at this thread's transition record in U-mode. Preserve
       temporaries, then restore the supervisor hart ID into tp before any C
       interrupt/syscall path executes. */
    sw t0, 64(sp)
    sw t1, 72(sp)
    sw tp, 80(sp)
    sw t2, 96(sp)
    csrr t0, sscratch
    sw t0, 88(sp)
    mv t2, sp
    lw tp, 56(t2)
    lw t0, 0(t2)
    csrw sscratch, zero
    mv sp, t0

    /* Save the user register frame on its kernel stack. */
    addi sp, sp, -144
    sw ra, 0(sp)
    lw t0, 64(t2)
    sw t0, 4(sp)
    lw t0, 72(t2)
    sw t0, 8(sp)
    lw t0, 96(t2)
    sw t0, 12(sp)
    sw s0, 16(sp)
    sw s1, 20(sp)
    sw a0, 24(sp)
    sw a1, 28(sp)
    sw a2, 32(sp)
    sw a3, 36(sp)
    sw a4, 40(sp)
    sw a5, 44(sp)
    sw a6, 48(sp)
    sw a7, 52(sp)
    sw s2, 56(sp)
    sw s3, 60(sp)
    sw s4, 64(sp)
    sw s5, 68(sp)
    sw s6, 72(sp)
    sw s7, 76(sp)
    sw s8, 80(sp)
    sw s9, 84(sp)
    sw s10, 88(sp)
    sw s11, 92(sp)
    sw t3, 96(sp)
    sw t4, 100(sp)
    sw t5, 104(sp)
    sw t6, 108(sp)

    /* Preserve user sp, tp and the transition record in the extended frame. */
    lw t0, 88(t2)
    sw t0, 112(sp)
    lw t0, 80(t2)
    sw t0, 128(sp)
    sw t2, 132(sp)

    csrr t0, sepc
    sw t0, 116(sp)
    csrr t1, sstatus
    sw t1, 120(sp)
    csrr t2, scause
    sw t2, 124(sp)

    /* Ensure SUM is set */
    li t0, (1 << 18)
    csrs sstatus, t0

    /* Check for ecall from U-mode (scause == 8) */
    bgez t2, .Luser_exception
    call xscore_device_irq_dispatch
    j .Luser_trap_return
.Luser_exception:
    li t4, 8
    beq t2, t4, .Lhandle_syscall

    /* Unhandled user exception: call C diagnostic handler */
    lw a0, 124(sp) /* scause */
    lw a1, 116(sp) /* sepc */
    csrr a2, stval /* stval */
    lw a3, 120(sp) /* sstatus */
    call xscore_trap_handler_c
    bnez a0, .Luser_trap_return
1:  wfi
    j 1b

.Lhandle_syscall:
    /* Advance sepc past ecall (4 bytes) */
    lw t0, 116(sp)
    addi t0, t0, 4
    sw t0, 116(sp)

    /* Prepare syscall dispatch arguments: a0=num (from a7), a1=arg0, a2=arg1, a3=arg2 */
    lw a0, 52(sp)   /* saved a7 */
    lw a1, 24(sp)   /* saved a0 */
    mv a1, sp
    call xscore_process_capture_syscall_context
    lw a0, 52(sp)
    lw a1, 24(sp)
    lw a2, 28(sp)
    lw a3, 32(sp)
.Ldispatch_syscall:
    call xscore_syscall_dispatch_riscv32

    /* Check if return value is XSCORE_SYSCALL_EXIT_TO_KERNEL (0x7FFFFFFF) */
    lui t0, 0x80000
    addi t0, t0, -1
    beq a0, t0, .Lsyscall_exit_kernel

    /* Normal return: write result to saved a0 */
    sw a0, 24(sp)

.Luser_trap_return:
    /* Preserve user-modified tp for this thread; if scheduling switched
       threads while handling the trap, use the new thread's TLS value. */
    call xscore_thread_current_user_transition
    mv t0, a0
    lw t1, 132(sp)
    beqz t0, .Luser_tp_saved
    beq t0, t1, .Luser_tp_saved
    lw tp, 48(t0)
    j .Luser_transition_ready
.Luser_tp_saved:
    lw tp, 128(sp)
    beqz t0, .Luser_transition_fallback
    j .Luser_transition_ready
.Luser_transition_fallback:
    mv t0, t1
.Luser_transition_ready:
    csrw sscratch, t0
    /* Restore sepc and sstatus */
    lw t0, 116(sp)
    csrw sepc, t0
    lw t1, 120(sp)
    csrw sstatus, t1

    /* Restore all GPRs */
    lw ra, 0(sp)
    lw t0, 4(sp)
    lw t1, 8(sp)
    lw t2, 12(sp)
    lw s0, 16(sp)
    lw s1, 20(sp)
    lw a0, 24(sp)
    lw a1, 28(sp)
    lw a2, 32(sp)
    lw a3, 36(sp)
    lw a4, 40(sp)
    lw a5, 44(sp)
    lw a6, 48(sp)
    lw a7, 52(sp)
    lw s2, 56(sp)
    lw s3, 60(sp)
    lw s4, 64(sp)
    lw s5, 68(sp)
    lw s6, 72(sp)
    lw s7, 76(sp)
    lw s8, 80(sp)
    lw s9, 84(sp)
    lw s10, 88(sp)
    lw s11, 92(sp)
    lw t3, 96(sp)
    lw t4, 100(sp)
    lw t5, 104(sp)
    lw t6, 108(sp)

    /* sscratch retains the transition record for the next trap. Recover the
       user stack from the frame after popping it. */
    addi sp, sp, 144
    lw sp, -32(sp)

    sret

.Lsyscall_exit_kernel:
    call xscore_thread_current_user_transition
    beqz a0, .Lhalt
    mv t0, a0
    sw zero, 24(t0)  /* active = 0 */
    lw sp, 0(t0)     /* kernel_sp */
    lw ra, 8(t0)     /* kernel_ra */
    lw s0, 0(sp)
    lw s1, 4(sp)
    lw s2, 8(sp)
    lw s3, 12(sp)
    lw s4, 16(sp)
    lw s5, 20(sp)
    lw s6, 24(sp)
    lw s7, 28(sp)
    lw s8, 32(sp)
    lw s9, 36(sp)
    lw s10, 40(sp)
    lw s11, 44(sp)
    addi sp, sp, 48
    ret

/* --------------------------------------------------------------------------
 * Context Switch: xscore_context_switch(old_ctx, new_ctx)
 * a0 = old_ctx (struct xscore_thread_context *)
 * a1 = new_ctx (struct xscore_thread_context *)
 * -------------------------------------------------------------------------- */
.global xscore_context_switch
.type xscore_context_switch, @function
xscore_context_switch:
    beqz a0, 1f
    sw s0, 0(a0)
    sw s1, 4(a0)
    sw s2, 8(a0)
    sw s3, 12(a0)
    sw s4, 16(a0)
    sw s5, 20(a0)
    sw s6, 24(a0)
    sw s7, 28(a0)
    sw s8, 32(a0)
    sw s9, 36(a0)
    sw s10, 40(a0)
    sw s11, 44(a0)
    sw ra, 48(a0)
    sw sp, 52(a0)
    csrr t0, sepc
    sw t0, 56(a0)
    csrr t0, sstatus
    sw t0, 60(a0)
    csrr t0, sscratch
    sw t0, 64(a0)

1:
    lw s0, 0(a1)
    lw s1, 4(a1)
    lw s2, 8(a1)
    lw s3, 12(a1)
    lw s4, 16(a1)
    lw s5, 20(a1)
    lw s6, 24(a1)
    lw s7, 28(a1)
    lw s8, 32(a1)
    lw s9, 36(a1)
    lw s10, 40(a1)
    lw s11, 44(a1)
    lw ra, 48(a1)
    lw sp, 52(a1)
    lw t0, 56(a1)
    csrw sepc, t0
    lw t0, 60(a1)
    csrw sstatus, t0
    lw t0, 64(a1)
    csrw sscratch, t0
    ret
.size xscore_context_switch, .-xscore_context_switch

/* --------------------------------------------------------------------------
 * User mode transition: xscore_user_enter(entry, sp, trans)
 * a0 = entry point
 * a1 = user stack pointer
 * a2 = transition struct pointer
 * -------------------------------------------------------------------------- */
.global xscore_user_enter
.type xscore_user_enter, @function
xscore_user_enter:
    /* a0 = entry (low), a1 = entry (high)
     * a2 = user sp (low), a3 = user sp (high)
     * a4 = transition struct pointer */
    /* Preserve the kernel SysV callee-saved registers across the complete
       user interval. The exit syscall resumes this stack directly instead
       of returning through the user trap frame. */
    /* Keep the initial sepc/sscratch/sstatus handoff atomic.  sret restores
       SPIE into SIE, so user code remains interruptible after the handoff. */
    csrci sstatus, 2
    addi sp, sp, -48
    sw s0, 0(sp)
    sw s1, 4(sp)
    sw s2, 8(sp)
    sw s3, 12(sp)
    sw s4, 16(sp)
    sw s5, 20(sp)
    sw s6, 24(sp)
    sw s7, 28(sp)
    sw s8, 32(sp)
    sw s9, 36(sp)
    sw s10, 40(sp)
    sw s11, 44(sp)

    /* Save kernel continuation */
    sw sp, 0(a4)
    sw ra, 8(a4)
    li t0, 1
    sw t0, 24(a4)
    sw tp, 56(a4)       /* Preserve the supervisor hart ID across U-mode. */

    /* Trap entry needs this record to recover the kernel stack and hart ID. */
    csrw sscratch, a4

    /* Set user entry in sepc */
    csrw sepc, a0

    /* Clear sstatus.SPP (enter U-mode on sret), set sstatus.SPIE, set sstatus.SUM */
    csrr t0, sstatus
    li t1, ~(1 << 8)
    and t0, t0, t1
    li t1, (1 << 5) | (1 << 18)
    or t0, t0, t1
    csrw sstatus, t0

    /* Set user stack pointer */
    mv sp, a2
    lw tp, 48(a4)
    lw a0, 40(a4)       /* first argument for a user thread */
    sret
.size xscore_user_enter, .-xscore_user_enter

.global xscore_user_resume
.type xscore_user_resume, @function
xscore_user_resume:
    /* A timer interrupt must not preempt the fork-frame copy or switch the
       current transition before sepc/sscratch/sstatus are restored.  sret
       below re-enables interrupts from the saved user status. */
    csrci sstatus, 2
    mv t3, a0
    addi sp, sp, -48
    sw s0, 0(sp)
    sw s1, 4(sp)
    sw s2, 8(sp)
    sw s3, 12(sp)
    sw s4, 16(sp)
    sw s5, 20(sp)
    sw s6, 24(sp)
    sw s7, 28(sp)
    sw s8, 32(sp)
    sw s9, 36(sp)
    sw s10, 40(sp)
    sw s11, 44(sp)
    sw sp, 0(a1)
    sw ra, 8(a1)
    li t0, 1
    sw t0, 24(a1)
    sw tp, 56(a1)

    addi sp, sp, -144
    mv t4, sp
    li t5, 36
.Lcopy_fork_frame:
    lw t0, 0(t3)
    sw t0, 0(t4)
    addi t3, t3, 4
    addi t4, t4, 4
    addi t5, t5, -1
    bnez t5, .Lcopy_fork_frame

    lw t0, 116(sp)
    csrw sepc, t0
    lw t1, 120(sp)
    csrw sstatus, t1
    csrw sscratch, a1
    /* a1 is the transition record and remains live while the saved frame is
       copied, so install its user TP before restoring user registers. */
    lw tp, 48(a1)
    lw ra, 0(sp)
    lw t0, 4(sp)
    lw t1, 8(sp)
    lw t2, 12(sp)
    lw s0, 16(sp)
    lw s1, 20(sp)
    lw a0, 24(sp)
    lw a1, 28(sp)
    lw a2, 32(sp)
    lw a3, 36(sp)
    lw a4, 40(sp)
    lw a5, 44(sp)
    lw a6, 48(sp)
    lw a7, 52(sp)
    lw s2, 56(sp)
    lw s3, 60(sp)
    lw s4, 64(sp)
    lw s5, 68(sp)
    lw s6, 72(sp)
    lw s7, 76(sp)
    lw s8, 80(sp)
    lw s9, 84(sp)
    lw s10, 88(sp)
    lw s11, 92(sp)
    lw t3, 96(sp)
    lw t4, 100(sp)
    lw t5, 104(sp)
    lw t6, 108(sp)
    addi sp, sp, 144
    lw sp, -32(sp)
    sret
.size xscore_user_resume, .-xscore_user_resume

.section .data
.align 4
.global boot_stack
boot_stack:
    .space 16384
boot_stack_top:
