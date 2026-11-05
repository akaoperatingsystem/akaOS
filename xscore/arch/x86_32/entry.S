.code32

/* Multiboot 1 & Multiboot 2 Header */
.section .multiboot, "a"
.align 4
/* Multiboot 1 */
.long 0x1BADB002                   /* Magic */
.long 0x00000007                   /* Flags: page aligned modules + meminfo + video mode */
.long -(0x1BADB002 + 0x00000007)   /* Checksum */
.long 0, 0, 0, 0, 0                /* Header/load addresses (0 for ELF) */
.long 0                            /* Mode type: 0 = linear graphics */
.long 1024                         /* Width: 1024 */
.long 768                          /* Height: 768 */
.long 32                           /* Depth: 32 bpp */

.align 8
/* Multiboot 2 */
mb2_header_start:
.long 0xe85250d6                   /* Magic */
.long 0                            /* Architecture: i386 (32-bit) */
.long mb2_header_end - mb2_header_start /* Header length */
.long -(0xe85250d6 + 0 + (mb2_header_end - mb2_header_start)) /* Checksum */

/* Framebuffer tag */
.align 8
.short 5                           /* Type = 5 (framebuffer) */
.short 0                           /* Flags */
.long 20                           /* Size = 20 bytes */
.long 1024                         /* Width */
.long 768                          /* Height */
.long 32                           /* Depth */

/* End tag */
.align 8
.short 0
.short 0
.long 8
mb2_header_end:

.section .text
.global _start32
.type _start32, @function
_start32:
    cli
    cld
    movl $boot_stack32_top, %esp
    pushl $0
    popfl

    /* Push arguments for xscore_multiboot_entry_32(uint32_t magic, uint32_t mb_info) */
    pushl %ebx
    pushl %eax
    call xscore_multiboot_entry_32

1:  hlt
    jmp 1b

/* GDT, IDT, TSS Loaders */
.global xscore_32_load_gdt
xscore_32_load_gdt:
    movl 4(%esp), %eax
    lgdt (%eax)
    ljmp $0x08, $1f
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    ret

.global xscore_32_load_idt
xscore_32_load_idt:
    movl 4(%esp), %eax
    lidt (%eax)
    ret

.global xscore_32_load_tss
xscore_32_load_tss:
    movw 4(%esp), %ax
    ltr %ax
    ret

.extern xscore_thread_current_user_transition
.extern xscore_32_set_user_tls_base

.global xscore_32_load_cr3
xscore_32_load_cr3:
    movl 4(%esp), %eax
    movl %eax, %cr3
    ret

.global xscore_32_enable_paging
xscore_32_enable_paging:
    movl %cr0, %eax
    orl $0x80010001, %eax   /* Set PG (bit 31), WP (bit 16), PE (bit 0) */
    movl %eax, %cr0
    ret

.global xscore_32_invlpg
xscore_32_invlpg:
    movl 4(%esp), %eax
    invlpg (%eax)
    ret

/* Stack enter: switches to dedicated kernel stack and enters target function */
.global xscore_stack_enter
.type xscore_stack_enter, @function
xscore_stack_enter:
    movl 4(%esp), %eax       /* new stack top (lower 32 bits) */
    movl 12(%esp), %edx      /* target function pointer (skip high 32 bits of stack_top) */
    movl 16(%esp), %ecx      /* argument low 32 bits */
    movl 20(%esp), %ebx      /* argument high 32 bits */
    movl %eax, %esp
    xorl %ebp, %ebp
    pushl %ebx               /* pass arg as 64-bit value on stack */
    pushl %ecx
    pushl $0                 /* fake return address */
    jmp *%edx

/* Port I/O helpers */
.global xscore_32_inb
xscore_32_inb:
    movw 4(%esp), %dx
    inb %dx, %al
    ret

.global xscore_32_outb
xscore_32_outb:
    movw 4(%esp), %dx
    movb 8(%esp), %al
    outb %al, %dx
    ret

.global xscore_32_inw
xscore_32_inw:
    movw 4(%esp), %dx
    inw %dx, %ax
    ret

.global xscore_32_outw
xscore_32_outw:
    movw 4(%esp), %dx
    movw 8(%esp), %ax
    outw %ax, %dx
    ret

.global xscore_32_inl
xscore_32_inl:
    movw 4(%esp), %dx
    inl %dx, %eax
    ret

.global xscore_32_outl
xscore_32_outl:
    movw 4(%esp), %dx
    movl 8(%esp), %eax
    outl %eax, %dx
    ret

/* Context switch for 32-bit kernel threads */
.global xscore_context_switch
.global xscore_context_switch_32
xscore_context_switch:
xscore_context_switch_32:
    /* arg1 (old_ctx) is at 4(%esp), arg2 (new_ctx) is at 8(%esp) */
    movl 4(%esp), %eax       /* eax = old_ctx */
    movl 8(%esp), %edx       /* edx = new_ctx */

    /* Save outgoing thread context */
    movl (%esp), %ecx        /* return address (EIP) */
    movl %ecx, 20(%eax)      /* context.eip */
    movl %esp, 24(%eax)      /* context.esp */
    pushfl
    popl %ecx
    movl %ecx, 16(%eax)      /* context.eflags */
    movl %ebp, 12(%eax)      /* context.ebp */
    movl %ebx, 8(%eax)       /* context.ebx */
    movl %esi, 4(%eax)       /* context.esi */
    movl %edi, 0(%eax)       /* context.edi */

    /* Restore incoming thread context */
    movl 0(%edx), %edi
    movl 4(%edx), %esi
    movl 8(%edx), %ebx
    movl 12(%edx), %ebp
    pushl 16(%edx)           /* push context.eflags */
    popfl
    movl 24(%edx), %esp      /* switch to the saved call-entry stack */
    addl $4, %esp            /* consume the saved return-address slot */
    jmp *20(%edx)            /* resume from context.eip, not stale stack data */

/* Transition to Ring 3 user mode */
.global xscore_user_enter
.global xscore_enter_user_32
xscore_user_enter:
xscore_enter_user_32:
    /* args in 32-bit cdecl with uint64_t:
       4(%esp)  = uint64_t eip (low 32 bits)
       8(%esp)  = uint64_t eip (high 32 bits)
       12(%esp) = uint64_t esp (low 32 bits)
       16(%esp) = uint64_t esp (high 32 bits)
       20(%esp) = transition pointer
    */
    /* Preserve the i386 SysV callee-saved registers across the complete
       Ring 3 interval.  The exit syscall deliberately leaves its interrupt
       frame on the privilege stack, so these values cannot be recovered
       from syscall_entry_32. */
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    /* Four saved registers moved the original cdecl arguments by 16 bytes. */
    movl 36(%esp), %eax  /* transition pointer */
    testl %eax, %eax
    jz .Lno_transition_32
    movl %esp, 0(%eax)   /* transition->kernel_rsp (low 32 bits) */
    movl $0, 4(%eax)     /* transition->kernel_rsp (high 32 bits) */
    movl $.Luser_resumed_32, 8(%eax) /* transition->kernel_rip (low 32 bits) */
    movl $0, 12(%eax)    /* transition->kernel_rip (high 32 bits) */
    movl $1, 24(%eax)    /* transition->active (low 32 bits) */
    movl $0, 28(%eax)    /* transition->active (high 32 bits) */

.Lno_transition_32:
    movl 20(%esp), %ecx  /* User EIP */
    movl 28(%esp), %edx  /* User ESP */
    movl 40(%eax), %ebp  /* first argument (before AX is reused for segments) */

    /* Set up user data segment registers */
    movw $0x23, %ax     /* User data selector: 0x20 | RPL 3 = 0x23 */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Push iret frame: SS, ESP, EFLAGS, CS, EIP */
    pushl $0x23          /* User SS: 0x20 | RPL 3 = 0x23 (User Data segment) */
    pushl %edx           /* User ESP */
    pushl $0x202         /* EFLAGS (IF = 1) */
    pushl $0x1B          /* User CS: 0x18 | RPL 3 = 0x1B (User Code segment) */
    pushl %ecx           /* User EIP */
    /* Install the current thread's GS base and reload the user TLS selector. */
    /* After the five iret pushes, the original transition argument is at
       56(%esp).  EAX now contains the user-data selector, not the pointer. */
    pushl 56(%esp)
    call xscore_32_set_user_tls_base
    addl $4, %esp
    movw $0x33, %ax
    movw %ax, %gs
    /* i386 userspace ELF programs find argc at (%esp), argv at 4(%esp).
     * Do NOT clobber 4(%edx) (argv[0])! */
    iret

.Luser_resumed_32:
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

/* Syscall entry int 0x80 */
.global syscall_entry_32
syscall_entry_32:
    /* Load kernel data segments for C handler execution */
    pushl %eax
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    popl %eax

    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx

    pushl %eax
    movl %esp, %edi
    addl $4, %edi
    pushl %edi
    pushl %eax
    call xscore_process_capture_syscall_context
    addl $8, %esp
    popl %eax
    movl 0(%esp), %ebx   /* reload arg0 */
    movl 4(%esp), %ecx   /* reload arg1 */
    movl 8(%esp), %edx   /* reload arg2 */
.Ldispatch_syscall_32:

    /* Push args for xscore_syscall_dispatch(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2) */
    pushl $0
    pushl %edx           /* arg2 (64-bit) */
    pushl $0
    pushl %ecx           /* arg1 (64-bit) */
    pushl $0
    pushl %ebx           /* arg0 (64-bit) */
    pushl $0
    pushl %eax           /* number (64-bit) */
    call xscore_syscall_dispatch
    addl $32, %esp       /* clean up 4 uint64_t arguments (32 bytes) */

    /* Check if return value is XSCORE_SYSCALL_EXIT_MAGIC (0x8000000000000000 or lower 32-bit = 0x7fffffff / 0x80000000) */
    cmpl $0x80000000, %edx
    je .Lsyscall_exit_32
    cmpl $0x7fffffff, %eax
    je .Lsyscall_exit_32

    /* Refresh the GS descriptor after SET_TP or a scheduler handoff. */
    pushl %eax
    call xscore_thread_current_user_transition
    testl %eax, %eax
    jz .Lsyscall_tp_ready_32
    pushl 48(%eax)
    call xscore_32_set_user_tls_base
    addl $4, %esp
    movw $0x33, %ax
    movw %ax, %gs
.Lsyscall_tp_ready_32:
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp

    /* Reload user data segments before returning to user space */
    pushl %eax
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    popl %eax

    iret

.Lsyscall_exit_32:
    call xscore_thread_current_user_transition
    testl %eax, %eax
    jz .Lsyscall_exit_halt_32
    movl $0, 24(%eax)    /* transition->active = 0 */
    movl 0(%eax), %esp   /* restore kernel esp */
    jmp *8(%eax)         /* jump to transition->kernel_rip */

.Lsyscall_exit_halt_32:
    cli
    hlt
    jmp .Lsyscall_exit_halt_32

.global xscore_user_resume
.type xscore_user_resume, @function
xscore_user_resume:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    movl 20(%esp), %esi       /* captured context */
    movl 24(%esp), %edx       /* transition */
    movl %esi, %eax            /* preserve context across the C setter */
    pushl %eax
    pushl 48(%edx)
    call xscore_32_set_user_tls_base
    addl $4, %esp
    popl %esi
    movw $0x33, %ax
    movw %ax, %gs
    movl %esp, 0(%edx)
    movl $0, 4(%edx)
    movl $.Luser_resume_return_32, 8(%edx)
    movl $0, 12(%edx)
    movl $1, 24(%edx)
    movl $0, 28(%edx)

    subl $44, %esp
    movl %esp, %edi
    movl $11, %ecx
    rep movsl

    /* Restore user data segments before returning to user space */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs

    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    xorl %eax, %eax
    iret

.Luser_resume_return_32:
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
.size xscore_user_resume, .-xscore_user_resume

/* Macro for Exception ISRs */
.macro ISR_NOERR index
.global isr\index
isr\index:
    pushl $0            /* Dummy error code */
    pushl $\index       /* Interrupt vector */
    jmp isr_common_32
.endm

.macro ISR_ERR index
.global isr\index
isr\index:
    pushl $\index       /* Interrupt vector */
    jmp isr_common_32
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

/* Hardware IRQs 0-15 mapped to vectors 32-47 */
.macro IRQ_STUB num vec
.global irq\num
irq\num:
    pushl $0
    pushl $\vec
    jmp isr_common_32
.endm

IRQ_STUB 0 32
IRQ_STUB 1 33
IRQ_STUB 2 34
IRQ_STUB 3 35
IRQ_STUB 4 36
IRQ_STUB 5 37
IRQ_STUB 6 38
IRQ_STUB 7 39
IRQ_STUB 8 40
IRQ_STUB 9 41
IRQ_STUB 10 42
IRQ_STUB 11 43
IRQ_STUB 12 44
IRQ_STUB 13 45
IRQ_STUB 14 46
IRQ_STUB 15 47

/* Common ISR Handler */
isr_common_32:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp           /* Pass pointer to trap frame */
    call xscore_32_trap_handler
    addl $4, %esp

    /* If the IRQ switched to another user TCB, refresh the shared GS
       descriptor before popping the saved user selector below. */
    call xscore_thread_current_user_transition
    testl %eax, %eax
    jz .Lirq_tls_ready_32
    pushl 48(%eax)
    call xscore_32_set_user_tls_base
    addl $4, %esp
.Lirq_tls_ready_32:

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp       /* Skip error code and interrupt number */
    iret

.section .bss
.align 16
boot_stack32:
    .skip 16384
boot_stack32_top:
