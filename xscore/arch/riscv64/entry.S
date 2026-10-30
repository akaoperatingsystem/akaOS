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
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    /* Call C kernel entry */
    call xscore_riscv64_main

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
.Lriscv64_secondary:
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
.Lriscv64_secondary_wait:
    lw t1, 0(t0)
    bnez t1, .Lriscv64_secondary_run
    nop
    j .Lriscv64_secondary_wait
.Lriscv64_secondary_run:
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
    addi sp, sp, -256
    sd ra, 0(sp)
    sd t0, 8(sp)
    sd t1, 16(sp)
    sd t2, 24(sp)
    sd a0, 48(sp)
    sd a1, 56(sp)
    sd a2, 64(sp)
    sd a3, 72(sp)
    sd a4, 80(sp)
    sd a5, 88(sp)
    sd a6, 96(sp)
    sd a7, 104(sp)
    sd t3, 192(sp)
    sd t4, 200(sp)
    sd t5, 208(sp)
    sd t6, 216(sp)
    csrr t0, sepc
    sd t0, 232(sp)
    csrr t1, sstatus
    sd t1, 240(sp)
    csrr t2, scause
    sd t2, 248(sp)

    /* Check if exception is fatal (bit 63 clear = exception, bit 63 set = interrupt) */
    srli t0, t2, 63
    beqz t0, .Lkernel_exception
    call xscore_device_irq_dispatch
    j .Lkernel_trap_return
.Lkernel_exception:

    /* Fatal kernel exception: call C diagnostic handler */
    ld a0, 248(sp) /* scause */
    ld a1, 232(sp) /* sepc */
    csrr a2, stval /* stval */
    ld a3, 240(sp) /* sstatus */
    li a4, 0       /* kernel trap has no user register frame */
    call xscore_trap_handler_c
1:  wfi
    j 1b

.Lkernel_trap_return:
    ld t0, 232(sp)
    csrw sepc, t0
    ld t1, 240(sp)
    csrw sstatus, t1
    ld ra, 0(sp)
    ld t0, 8(sp)
    ld t1, 16(sp)
    ld t2, 24(sp)
    ld a0, 48(sp)
    ld a1, 56(sp)
    ld a2, 64(sp)
    ld a3, 72(sp)
    ld a4, 80(sp)
    ld a5, 88(sp)
    ld a6, 96(sp)
    ld a7, 104(sp)
    ld t3, 192(sp)
    ld t4, 200(sp)
    ld t5, 208(sp)
    ld t6, 216(sp)
    addi sp, sp, 256
    sret

.Ltrap_from_user:
    /* In U-mode sscratch holds the transition record, not just the kernel
       stack. Preserve temporaries, then restore the supervisor hart ID into
       tp before entering C interrupt/syscall code. */
    sd t0, 64(sp)
    sd t1, 72(sp)
    sd tp, 80(sp)
    sd t2, 96(sp)
    csrr t0, sscratch
    sd t0, 88(sp)
    mv t2, sp
    ld tp, 56(t2)
    ld t0, 0(t2)
    csrw sscratch, zero
    mv sp, t0

    /* Save the user register frame on its kernel stack. */
    addi sp, sp, -272
    sd ra, 0(sp)
    ld t0, 64(t2)
    sd t0, 8(sp)
    ld t0, 72(t2)
    sd t0, 16(sp)
    ld t0, 96(t2)
    sd t0, 24(sp)
    sd s0, 32(sp)
    sd s1, 40(sp)
    sd a0, 48(sp)
    sd a1, 56(sp)
    sd a2, 64(sp)
    sd a3, 72(sp)
    sd a4, 80(sp)
    sd a5, 88(sp)
    sd a6, 96(sp)
    sd a7, 104(sp)
    sd s2, 112(sp)
    sd s3, 120(sp)
    sd s4, 128(sp)
    sd s5, 136(sp)
    sd s6, 144(sp)
    sd s7, 152(sp)
    sd s8, 160(sp)
    sd s9, 168(sp)
    sd s10, 176(sp)
    sd s11, 184(sp)
    sd t3, 192(sp)
    sd t4, 200(sp)
    sd t5, 208(sp)
    sd t6, 216(sp)

    /* Preserve user sp, tp and the transition record in the extended frame. */
    ld t0, 88(t2)
    sd t0, 224(sp)
    ld t0, 80(t2)
    sd t0, 256(sp)
    sd t2, 264(sp)

    csrr t0, sepc
    sd t0, 232(sp)
    csrr t1, sstatus
    sd t1, 240(sp)
    csrr t2, scause
    sd t2, 248(sp)

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

    /* Unhandled exception: call C diagnostic handler */
    ld a0, 248(sp) /* scause */
    ld a1, 232(sp) /* sepc */
    csrr a2, stval /* stval */
    ld a3, 240(sp) /* sstatus */
    mv a4, sp      /* extended user register frame for diagnostics */
    call xscore_trap_handler_c
    bnez a0, .Luser_trap_return
1:  wfi
    j 1b

.Lhandle_syscall:
    /* Advance sepc past ecall (4 bytes) */
    ld t0, 232(sp)
    addi t0, t0, 4
    sd t0, 232(sp)

    /* Prepare syscall dispatch arguments: a0=num (from a7), a1=arg0, a2=arg1, a3=arg2 */
    ld a0, 104(sp)  /* saved a7 */
    ld a1, 48(sp)   /* saved a0 */
    mv a1, sp
    call xscore_process_capture_syscall_context
    ld a0, 104(sp)
    ld a1, 48(sp)
    ld a2, 56(sp)
    ld a3, 64(sp)
.Ldispatch_syscall:
    call xscore_syscall_dispatch_riscv64

    /* Check if return value is XSCORE_SYSCALL_EXIT_TO_KERNEL (0x7FFFFFFF or 0x8000000000000000) */
    li t0, 0x7fffffff
    beq a0, t0, .Lsyscall_exit_kernel
    li t0, 1
    slli t0, t0, 63
    beq a0, t0, .Lsyscall_exit_kernel

    /* Normal return: write result to saved a0 */
    sd a0, 48(sp)

.Luser_trap_return:
    /* Preserve user-modified tp when this is still the same thread; if the
       timer scheduler switched threads, install the new thread's TLS value. */
    call xscore_thread_current_user_transition
    mv t0, a0
    ld t1, 264(sp)
    beqz t0, .Luser_tp_saved
    beq t0, t1, .Luser_tp_saved
    ld tp, 48(t0)
    j .Luser_transition_ready
.Luser_tp_saved:
    ld tp, 256(sp)
    beqz t0, .Luser_transition_fallback
    j .Luser_transition_ready
.Luser_transition_fallback:
    mv t0, t1
.Luser_transition_ready:
    csrw sscratch, t0
    /* Restore sepc and sstatus */
    ld t0, 232(sp)
    csrw sepc, t0
    ld t1, 240(sp)
    csrw sstatus, t1

    /* Restore all GPRs */
    ld ra, 0(sp)
    ld t0, 8(sp)
    ld t1, 16(sp)
    ld t2, 24(sp)
    ld s0, 32(sp)
    ld s1, 40(sp)
    ld a0, 48(sp)
    ld a1, 56(sp)
    ld a2, 64(sp)
    ld a3, 72(sp)
    ld a4, 80(sp)
    ld a5, 88(sp)
    ld a6, 96(sp)
    ld a7, 104(sp)
    ld s2, 112(sp)
    ld s3, 120(sp)
    ld s4, 128(sp)
    ld s5, 136(sp)
    ld s6, 144(sp)
    ld s7, 152(sp)
    ld s8, 160(sp)
    ld s9, 168(sp)
    ld s10, 176(sp)
    ld s11, 184(sp)
    ld t3, 192(sp)
    ld t4, 200(sp)
    ld t5, 208(sp)
    ld t6, 216(sp)

    /* sscratch retains the transition record for the next trap. Recover the
       user stack from the still-mapped frame after popping it. */
    addi sp, sp, 272
    ld sp, -48(sp)

    sret

.Lsyscall_exit_kernel:
    call xscore_thread_current_user_transition
    beqz a0, .Lhalt
    mv t0, a0
    sd zero, 24(t0)  /* active = 0 */
    ld sp, 0(t0)     /* kernel_sp */
    ld ra, 8(t0)     /* kernel_ra */
    ld s0, 0(sp)
    ld s1, 8(sp)
    ld s2, 16(sp)
    ld s3, 24(sp)
    ld s4, 32(sp)
    ld s5, 40(sp)
    ld s6, 48(sp)
    ld s7, 56(sp)
    ld s8, 64(sp)
    ld s9, 72(sp)
    ld s10, 80(sp)
    ld s11, 88(sp)
    addi sp, sp, 96
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
    sd s0, 0(a0)
    sd s1, 8(a0)
    sd s2, 16(a0)
    sd s3, 24(a0)
    sd s4, 32(a0)
    sd s5, 40(a0)
    sd s6, 48(a0)
    sd s7, 56(a0)
    sd s8, 64(a0)
    sd s9, 72(a0)
    sd s10, 80(a0)
    sd s11, 88(a0)
    sd ra, 96(a0)
    sd sp, 104(a0)
    csrr t0, sepc
    sd t0, 112(a0)
    csrr t0, sstatus
    sd t0, 120(a0)
    csrr t0, sscratch
    sd t0, 128(a0)

1:
    ld s0, 0(a1)
    ld s1, 8(a1)
    ld s2, 16(a1)
    ld s3, 24(a1)
    ld s4, 32(a1)
    ld s5, 40(a1)
    ld s6, 48(a1)
    ld s7, 56(a1)
    ld s8, 64(a1)
    ld s9, 72(a1)
    ld s10, 80(a1)
    ld s11, 88(a1)
    ld ra, 96(a1)
    ld sp, 104(a1)
    ld t0, 112(a1)
    csrw sepc, t0
    ld t0, 120(a1)
    csrw sstatus, t0
    ld t0, 128(a1)
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
    /* Preserve the kernel SysV callee-saved registers across the complete
       user interval. The exit syscall resumes this stack directly instead
       of returning through the user trap frame. */
    /* Do not let the timer switch TCBs while sepc/sscratch/sstatus are being
       assembled.  sret transfers SPIE into SIE, so the user context still
       starts with interrupts enabled. */
    csrci sstatus, 2
    addi sp, sp, -96
    sd s0, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    sd s5, 40(sp)
    sd s6, 48(sp)
    sd s7, 56(sp)
    sd s8, 64(sp)
    sd s9, 72(sp)
    sd s10, 80(sp)
    sd s11, 88(sp)

    /* Save kernel continuation */
    sd sp, 0(a2)
    sd ra, 8(a2)
    li t0, 1
    sd t0, 24(a2)
    sd tp, 56(a2)       /* Preserve the supervisor hart ID across U-mode. */

    /* Trap entry needs this record to recover the kernel stack and hart ID. */
    csrw sscratch, a2

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
    mv sp, a1
    ld tp, 48(a2)
    ld a0, 40(a2)       /* first argument for a user thread */
    sret
.size xscore_user_enter, .-xscore_user_enter

.global xscore_user_resume
.type xscore_user_resume, @function
xscore_user_resume:
    /* Keep the saved user frame and transition update atomic.  The sret at
       the end restores the saved SPIE/SIE state for user execution. */
    csrci sstatus, 2
    mv t3, a0
    addi sp, sp, -96
    sd s0, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    sd s5, 40(sp)
    sd s6, 48(sp)
    sd s7, 56(sp)
    sd s8, 64(sp)
    sd s9, 72(sp)
    sd s10, 80(sp)
    sd s11, 88(sp)
    sd sp, 0(a1)
    sd ra, 8(a1)
    li t0, 1
    sd t0, 24(a1)
    sd tp, 56(a1)

    addi sp, sp, -272
    mv t4, sp
    li t5, 34
.Lcopy_fork_frame:
    ld t0, 0(t3)
    sd t0, 0(t4)
    addi t3, t3, 8
    addi t4, t4, 8
    addi t5, t5, -1
    bnez t5, .Lcopy_fork_frame

    ld t0, 232(sp)
    csrw sepc, t0
    ld t1, 240(sp)
    csrw sstatus, t1
    csrw sscratch, a1
    /* a1 is the transition record and has not been overwritten by the frame
       copy; install its user TP before restoring user registers. */
    ld tp, 48(a1)
    ld ra, 0(sp)
    ld t0, 8(sp)
    ld t1, 16(sp)
    ld t2, 24(sp)
    ld s0, 32(sp)
    ld s1, 40(sp)
    ld a0, 48(sp)
    ld a1, 56(sp)
    ld a2, 64(sp)
    ld a3, 72(sp)
    ld a4, 80(sp)
    ld a5, 88(sp)
    ld a6, 96(sp)
    ld a7, 104(sp)
    ld s2, 112(sp)
    ld s3, 120(sp)
    ld s4, 128(sp)
    ld s5, 136(sp)
    ld s6, 144(sp)
    ld s7, 152(sp)
    ld s8, 160(sp)
    ld s9, 168(sp)
    ld s10, 176(sp)
    ld s11, 184(sp)
    ld t3, 192(sp)
    ld t4, 200(sp)
    ld t5, 208(sp)
    ld t6, 216(sp)
    addi sp, sp, 272
    ld sp, -48(sp)
    sret
.size xscore_user_resume, .-xscore_user_resume

.section .data
.align 4
.global boot_stack
boot_stack:
    .space 16384
boot_stack_top:
