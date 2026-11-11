.section .multiboot,"a",@progbits
.align 8
.Lmultiboot_header_start:
.long 0xe85250d6                  /* Multiboot2 header magic */
.long 0                               /* i386 protected-mode architecture */
.long .Lmultiboot_header_end - .Lmultiboot_header_start
.long 0x17adaeea                      /* header checksum */

/* Tell GRUB to enter through the 32-bit-to-long-mode bridge below. */
.short 3                              /* entry-address tag */
.short 0
.long 12
.long xscore_multiboot_entry
.long 0                               /* align the next tag to eight bytes */

/* Request a 1024x768x32 linear framebuffer when the firmware offers one. */
.short 5                              /* framebuffer tag */
.short 0
.long 20
.long 1024
.long 768
.long 32
.long 0                               /* tag padding */

.short 0                              /* end tag */
.short 0
.long 8
.Lmultiboot_header_end:

.section .text.multiboot_entry,"ax",@progbits
.code32
.global xscore_multiboot_entry
.type xscore_multiboot_entry, @function
xscore_multiboot_entry:
    cli
    cmpl $0x36d76289, %eax            /* MULTIBOOT2_BOOTLOADER_MAGIC */
    jne .Lmultiboot_halt32
    movl %ebx, mb_multiboot_info_ptr

    /* Build an identity map for the first 1 GiB while still in 32-bit mode. */
    cld
    movl $mb_pml4, %edi
    xorl %eax, %eax
    movl $1024, %ecx
    rep stosl

    movl $mb_pdpt, %eax
    orl $0x3, %eax
    movl %eax, mb_pml4
    movl $mb_pd, %eax
    orl $0x3, %eax
    movl %eax, mb_pdpt
    movl $mb_pd + 4096, %eax
    orl $0x3, %eax
    movl %eax, mb_pdpt + 8
    movl $mb_pd + 8192, %eax
    orl $0x3, %eax
    movl %eax, mb_pdpt + 16
    movl $mb_pd + 12288, %eax
    orl $0x3, %eax
    movl %eax, mb_pdpt + 24

    movl $mb_pd, %edi
    xorl %edx, %edx
    movl $2048, %ecx
.Lfill_boot_pd:
    movl %edx, %eax
    orl $0x83, %eax                    /* present, writable, 2 MiB page */
    stosl
    xorl %eax, %eax
    stosl
    addl $0x200000, %edx
    loop .Lfill_boot_pd

    movl $mb_pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x620, %eax                    /* PAE, OSFXSR, OSXMMEXCPT */
    movl %eax, %cr4
    movl $0xc0000080, %ecx              /* EFER */
    rdmsr
    orl $0x100, %eax                    /* LME */
    wrmsr
    movl %cr0, %eax
    andl $0xfffffff3, %eax              /* clear CR0.EM and CR0.TS */
    orl $0x80000002, %eax               /* paging and CR0.MP */
    movl %eax, %cr0

    lgdt mb_boot_gdt_pointer
    ljmp $0x08, $mb_long_mode_entry

.Lmultiboot_halt32:
    hlt
    jmp .Lmultiboot_halt32
.size xscore_multiboot_entry, .-xscore_multiboot_entry

.code64
.type mb_long_mode_entry, @function
mb_long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movq $mb_boot_stack_top, %rsp
    xorq %rbp, %rbp
    movl mb_multiboot_info_ptr(%rip), %edi
    call xscore_bios_entry
.Lmultiboot_halt64:
    cli
    hlt
    jmp .Lmultiboot_halt64
.size mb_long_mode_entry, .-mb_long_mode_entry

.section .rodata.mb_boot,"a",@progbits
.align 8
mb_boot_gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff             /* 64-bit code */
    .quad 0x00cf92000000ffff             /* data */
mb_boot_gdt_pointer:
    .word 23
    .long mb_boot_gdt

.section .bss.mb_boot,"aw",@nobits
.align 4096
mb_pml4:
    .skip 4096
mb_pdpt:
    .skip 4096
mb_pd:
    .skip 16384
.align 8
mb_multiboot_info_ptr:
    .long 0
.align 16
mb_boot_stack:
    .skip 16384
mb_boot_stack_top:

.section .text

/* Program the architectural FS base used by the x86-64 static-TLS ABI.
 * This is called only in ring 0 immediately before returning to a user
 * context, so WRMSR cannot expose a transient kernel value to user code. */
.global xscore_arch_set_user_tp
.type xscore_arch_set_user_tp, @function
xscore_arch_set_user_tp:
    movq %rdi, %rax
    movl $0xC0000100, %ecx       /* IA32_FS_BASE */
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr
    ret
.size xscore_arch_set_user_tp, .-xscore_arch_set_user_tp

/* ============================================================
 * GDT and IDT load routines
 * ============================================================ */

.global xscore_gdt_load
.type xscore_gdt_load, @function
xscore_gdt_load:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lgdt_reloaded(%rip), %rax
    pushq %rax
    lretq
.Lgdt_reloaded:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    ret
.size xscore_gdt_load, .-xscore_gdt_load

.global xscore_idt_load
.type xscore_idt_load, @function
xscore_idt_load:
    lidt (%rdi)
    ret
.size xscore_idt_load, .-xscore_idt_load

/* ============================================================
 * TSS load routine
 * ============================================================ */

.global xscore_tss_load
.type xscore_tss_load, @function
xscore_tss_load:
    ltr %di
    ret
.size xscore_tss_load, .-xscore_tss_load

/* ============================================================
 * Stack entry routine
 *
 * Switches RSP to a fresh dedicated kernel stack and starts
 * executing the target function there. Never returns.
 *
 *   RDI = new stack top (16-byte aligned)
 *   RSI = target function (receives its first argument in RDI)
 *   RDX = argument passed to the target function
 *
 * The target must never return: its caller chain, registers, and
 * return address all live on the old stack, which is abandoned.
 * A fake zero return address is pushed so the stack layout matches
 * a normal function call (RSP % 16 == 8 at entry).
 * ============================================================ */

.global xscore_stack_enter
.type xscore_stack_enter, @function
xscore_stack_enter:
    movq %rdi, %rsp
    xorq %rbp, %rbp     /* clear frame pointer for the new chain */
    movq %rdx, %rdi     /* forward the argument to the target */
    pushq $0            /* fake return address */
    jmp *%rsi
.size xscore_stack_enter, .-xscore_stack_enter

/* ============================================================
 * CPU exception stubs (vectors 0–31)
 * ============================================================ */

.macro ISR_NO_ERROR vector
.global xscore_isr_\vector
xscore_isr_\vector:
    pushq $0
    pushq $\vector
    jmp xscore_isr_common
.endm

.macro ISR_ERROR vector
.global xscore_isr_\vector
xscore_isr_\vector:
    pushq $\vector
    jmp xscore_isr_common
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31

.type xscore_isr_common, @function
xscore_isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx             /* saved GPR frame pointer */
    leaq 120(%rsp), %rdx        /* CPU frame: vector, error, rip, cs, rflags, rsp, ss */
    movq 120(%rsp), %rdi        /* vector */
    movq 128(%rsp), %rsi        /* error code */
    andq $-16, %rsp
    call xscore_exception_handler
    movq %rbx, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size xscore_isr_common, .-xscore_isr_common

/* ============================================================
 * Hardware IRQ stubs (vectors 32–47)
 * These push a dummy error code and the vector number,
 * then dispatch to xscore_irq_common which calls the
 * appropriate C handler.
 * ============================================================ */

.macro IRQ_STUB irq_number
.global xscore_irq_\irq_number
xscore_irq_\irq_number:
    pushq $0
    pushq $(\irq_number + 32)
    jmp xscore_irq_common
.endm

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

/* Local-APIC reschedule IPI. */
.global xscore_irq_reschedule
xscore_irq_reschedule:
    pushq $0
    pushq $254
    jmp xscore_irq_common

/* Local-APIC TLB shootdown IPI. */
.global xscore_irq_tlb_shootdown
xscore_irq_tlb_shootdown:
    pushq $0
    pushq $253
    jmp xscore_irq_common

.global xscore_irq_spurious
xscore_irq_spurious:
    iretq

.type xscore_irq_common, @function
xscore_irq_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* RDI = vector number (on stack at offset 120) */
    movq 120(%rsp), %rdi
    movq %rsp, %rsi             /* frame pointer for preemption check */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_irq_dispatch
    movq %rbx, %rsp

    /* Timer-driven scheduling may have selected another user TCB. */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_thread_current_user_transition
    movq %rbx, %rsp
    testq %rax, %rax
    jz .Lirq_tp_ready
    movq 48(%rax), %rdi
    call xscore_arch_set_user_tp
.Lirq_tp_ready:

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size xscore_irq_common, .-xscore_irq_common

/* ============================================================
 * Ring 3 entry and int 0x80 system-call path
 * ============================================================ */

.global xscore_user_enter
.type xscore_user_enter, @function
xscore_user_enter:
    /*
     * RDX points at the current TCB's xscore_user_transition. Preserve the
     * kernel ABI state there so separate tasks never share return storage.
     */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, 0(%rdx)
    leaq .Luser_return(%rip), %rax
    movq %rax, 8(%rdx)
    movq $1, 24(%rdx)

    /* iretq frame: SS, RSP, RFLAGS, CS, RIP. */
    pushq $0x1b
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    /* Newly-created user threads receive their first argument in RDI. */
    movq 40(%rdx), %r12
    movq 48(%rdx), %rdi
    call xscore_arch_set_user_tp
    movq %r12, %rdi
    iretq

.Luser_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size xscore_user_enter, .-xscore_user_enter

.global xscore_syscall_int80
.type xscore_syscall_int80, @function
xscore_syscall_int80:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx
    
    /* FAST PATH CHECK: Must run before context capture */
    movq 112(%rbx), %rdi       /* syscall number */
    movq 72(%rbx), %rsi        /* argument 0 */
    movq 64(%rbx), %rdx        /* argument 1 */
    movq 88(%rbx), %rcx        /* argument 2 */
    andq $-16, %rsp
    call xscore_syscall_fast_dispatch
    movq %rbx, %rsp
    
    /* Check for NOT_HANDLED (0xFFFFFFFFFFFFFFFFUL) */
    movabsq $0xFFFFFFFFFFFFFFFF, %r10
    cmpq %r10, %rax
    je .Lslow_path
    
    /* Handled by fast path - exit immediately */
    movq %rax, 112(%rbx)
    jmp .Lsyscall_exit

.Lslow_path:
    movq %rsp, %rbx
    movq 112(%rbx), %rdi
    movq %rbx, %rsi
    andq $-16, %rsp
    call xscore_process_capture_syscall_context
    movq %rbx, %rsp
.Lsyscall_dispatch:
    movq 112(%rbx), %rdi       /* syscall number (user RAX) */
    movq 72(%rbx), %rsi        /* argument 0 (user RDI) */
    movq 64(%rbx), %rdx        /* argument 1 (user RSI) */
    movq 88(%rbx), %rcx        /* argument 2 (user RDX) */
    andq $-16, %rsp
    call xscore_syscall_dispatch
    movq %rbx, %rsp
    movabsq $0x8000000000000000, %r10
    cmpq %r10, %rax
    je .Lsyscall_exit
    movq %rax, 112(%rsp)       /* return value becomes user RAX */

    /* SET_TP and scheduler returns both need the architectural FS base
       updated before the register frame is popped and iretq executes. */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_thread_current_user_transition
    movq %rbx, %rsp
    testq %rax, %rax
    jz .Lsyscall_tp_ready
    movq 48(%rax), %rdi
    call xscore_arch_set_user_tp
.Lsyscall_tp_ready:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size xscore_isr_common, .-xscore_isr_common

/* ============================================================
 * Hardware IRQ stubs (vectors 32–47)
 * These push a dummy error code and the vector number,
 * then dispatch to xscore_irq_common which calls the
 * appropriate C handler.
 * ============================================================ */

.macro IRQ_STUB irq_number
.global xscore_irq_\irq_number
xscore_irq_\irq_number:
    pushq $0
    pushq $(\irq_number + 32)
    jmp xscore_irq_common
.endm

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

/* Local-APIC reschedule IPI. */
.global xscore_irq_reschedule
xscore_irq_reschedule:
    pushq $0
    pushq $254
    jmp xscore_irq_common

/* Local-APIC TLB shootdown IPI. */
.global xscore_irq_tlb_shootdown
xscore_irq_tlb_shootdown:
    pushq $0
    pushq $253
    jmp xscore_irq_common

.global xscore_irq_spurious
xscore_irq_spurious:
    iretq

.type xscore_irq_common, @function
xscore_irq_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* RDI = vector number (on stack at offset 120) */
    movq 120(%rsp), %rdi
    movq %rsp, %rsi             /* frame pointer for preemption check */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_irq_dispatch
    movq %rbx, %rsp

    /* Timer-driven scheduling may have selected another user TCB. */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_thread_current_user_transition
    movq %rbx, %rsp
    testq %rax, %rax
    jz .Lirq_tp_ready
    movq 48(%rax), %rdi
    call xscore_arch_set_user_tp
.Lirq_tp_ready:

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size xscore_irq_common, .-xscore_irq_common

/* ============================================================
 * Ring 3 entry and int 0x80 system-call path
 * ============================================================ */

.global xscore_user_enter
.type xscore_user_enter, @function
xscore_user_enter:
    /*
     * RDX points at the current TCB's xscore_user_transition. Preserve the
     * kernel ABI state there so separate tasks never share return storage.
     */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, 0(%rdx)
    leaq .Luser_return(%rip), %rax
    movq %rax, 8(%rdx)
    movq $1, 24(%rdx)

    /* iretq frame: SS, RSP, RFLAGS, CS, RIP. */
    pushq $0x1b
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    /* Newly-created user threads receive their first argument in RDI. */
    movq 40(%rdx), %r12
    movq 48(%rdx), %rdi
    call xscore_arch_set_user_tp
    movq %r12, %rdi
    iretq

.Luser_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size xscore_user_enter, .-xscore_user_enter

.global xscore_syscall_int80
.type xscore_syscall_int80, @function
xscore_syscall_int80:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx
    movq 112(%rbx), %rdi
    movq %rbx, %rsi
    andq $-16, %rsp
    call xscore_process_capture_syscall_context
    movq %rbx, %rsp

    /* Fast path check */
    movq 112(%rbx), %rdi       /* syscall number */
    movq 72(%rbx), %rsi        /* argument 0 */
    movq 64(%rbx), %rdx        /* argument 1 */
    movq 88(%rbx), %rcx        /* argument 2 */
    andq $-16, %rsp
    call xscore_syscall_fast_dispatch
    movq %rbx, %rsp
    movabsq $0xFFFFFFFFFFFFFFFF, %r10
    cmpq %r10, %rax
    je .Lslow_path_dispatch
    movq %rax, 112(%rbx)
    jmp .Lsyscall_exit

.Lslow_path_dispatch:
.Lsyscall_dispatch:
    movq 112(%rbx), %rdi       /* syscall number (user RAX) */
    movq 72(%rbx), %rsi        /* argument 0 (user RDI) */
    movq 64(%rbx), %rdx        /* argument 1 (user RSI) */
    movq 88(%rbx), %rcx        /* argument 2 (user RDX) */
    andq $-16, %rsp
    call xscore_syscall_dispatch
    movq %rbx, %rsp
    movabsq $0x8000000000000000, %r10
    cmpq %r10, %rax
    je .Lsyscall_exit
    movq %rax, 112(%rsp)       /* return value becomes user RAX */

    /* SET_TP and scheduler returns both need the architectural FS base
       updated before the register frame is popped and iretq executes. */
    movq %rsp, %rbx
    andq $-16, %rsp
    call xscore_thread_current_user_transition
    movq %rbx, %rsp
    testq %rax, %rax
    jz .Lsyscall_tp_ready
    movq 48(%rax), %rdi
    call xscore_arch_set_user_tp
.Lsyscall_tp_ready:

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq

.Lsyscall_exit:
    /* Resolve the transition record from the currently scheduled TCB. */
    andq $-16, %rsp
    call xscore_thread_current_user_transition
    testq %rax, %rax
    jz .Lsyscall_exit_halt
    movq $0, 24(%rax)
    movq 0(%rax), %rsp
    jmp *8(%rax)
.Lsyscall_exit_halt:
    cli
    hlt
    jmp .Lsyscall_exit_halt
.size xscore_syscall_int80, .-xscore_syscall_int80

.global xscore_user_resume
.type xscore_user_resume, @function
xscore_user_resume:
    /* Preserve the same kernel ABI state as xscore_user_enter. */
    movq %rdi, %rdx
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* Keep the captured frame in a callee-saved scratch register; the TP
       WRMSR helper legitimately clobbers RAX/RDX. */
    movq %rdx, %r13
    movq %rsp, 0(%rsi)
    leaq .Luser_resume_return(%rip), %rax
    movq %rax, 8(%rsi)
    movq $1, 24(%rsi)
    movq 48(%rsi), %rdi
    call xscore_arch_set_user_tp

    /* Copy the captured 20-qword syscall/iret frame to this task's stack. */
    subq $160, %rsp
    movq %rsp, %rdi
    movq %r13, %rsi
    movq $20, %rcx
    rep movsq

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq

.Luser_resume_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size xscore_user_resume, .-xscore_user_resume

/* ============================================================
 * Read-only data
 * ============================================================ */

.section .rodata
.global xscore_isr_stub_table
.type xscore_isr_stub_table, @object
xscore_isr_stub_table:
    .quad xscore_isr_0, xscore_isr_1, xscore_isr_2, xscore_isr_3
    .quad xscore_isr_4, xscore_isr_5, xscore_isr_6, xscore_isr_7
    .quad xscore_isr_8, xscore_isr_9, xscore_isr_10, xscore_isr_11
    .quad xscore_isr_12, xscore_isr_13, xscore_isr_14, xscore_isr_15
    .quad xscore_isr_16, xscore_isr_17, xscore_isr_18, xscore_isr_19
    .quad xscore_isr_20, xscore_isr_21, xscore_isr_22, xscore_isr_23
    .quad xscore_isr_24, xscore_isr_25, xscore_isr_26, xscore_isr_27
    .quad xscore_isr_28, xscore_isr_29, xscore_isr_30, xscore_isr_31
.size xscore_isr_stub_table, .-xscore_isr_stub_table

.section .text
/* ============================================================
 * Kernel thread context switch (Phase 3 — Multitasking)
 *
 * xscore_context_switch(old_ctx, new_ctx)
 *   RDI = pointer to the outgoing thread's xscore_thread_context
 *   RSI = pointer to the incoming thread's xscore_thread_context
 *
 * Saves the callee-saved registers of the outgoing thread into old_ctx
 * and restores the callee-saved registers of the incoming thread from
 * new_ctx, then returns into the incoming thread. The layout of
 * struct xscore_thread_context must match the offsets used here.
 * ============================================================ */

.global xscore_context_switch
.type xscore_context_switch, @function
xscore_context_switch:
    /* Save outgoing thread's callee-saved registers.
     * struct xscore_thread_context offsets:
     *   r15=0 r14=8 r13=16 r12=24 rbx=32 rbp=40 rflags=48 rip=56 rsp=64
     *
     * The saved rsp points at the return address that is already on the
     * outgoing stack (pushed by the call to this function), and context.rip
     * holds that same return address. On restore we set rsp to the incoming
     * saved value and `ret`, which pops the return address that already
     * lives at (%rsp) — so we must NOT push rip/rflags on top of it.
     */
    movq (%rsp), %rax         /* return address from the switch call frame */
    movq %rax, 56(%rdi)       /* context.rip */
    movq %rsp, 64(%rdi)       /* context.rsp (points at return address) */
    pushfq
    popq %rax
    movq %rax, 48(%rdi)       /* context.rflags */
    movq %rbp, 40(%rdi)       /* context.rbp */
    movq %rbx, 32(%rdi)       /* context.rbx */
    movq %r12, 24(%rdi)       /* context.r12 */
    movq %r13, 16(%rdi)       /* context.r13 */
    movq %r14, 8(%rdi)        /* context.r14 */
    movq %r15, (%rdi)         /* context.r15 */

    /* Restore incoming thread's callee-saved registers. */
    movq (%rsi), %r15
    movq 8(%rsi), %r14
    movq 16(%rsi), %r13
    movq 24(%rsi), %r12
    movq 32(%rsi), %rbx
    movq 40(%rsi), %rbp
    pushq 48(%rsi)           /* push context.rflags */
    popfq
    movq 64(%rsi), %rsp      /* context.rsp — switch stacks (points at saved return addr) */
    ret                       /* pop and jump to the return address at (%rsp) */
.size xscore_context_switch, .-xscore_context_switch

.section .note.GNU-stack,"",@progbits
