.section .text.smp_trampoline, "ax", @progbits
.global smp_trampoline_start
.global smp_trampoline_end
.global smp_boot_cr3
.global smp_boot_stack
.global smp_boot_entry
.global smp_boot_status

smp_trampoline_start:
.code16
    cli
    cld

    /* Set segments to 0 */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* Load temporary 32-bit GDT */
    lgdtl (0x8000 + (smp_trampoline_gdt_ptr - smp_trampoline_start))

    /* Enable Protected Mode (CR0.PE = 1) */
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    /* Far jump to 32-bit protected mode code segment (selector 0x08) */
    ljmpl $0x08, $(0x8000 + (smp_trampoline_pm32 - smp_trampoline_start))

.code32
smp_trampoline_pm32:
    /* Set 32-bit data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* Enable PAE in CR4 (bit 5) */
    movl %cr4, %eax
    orl $0x620, %eax                    /* PAE, OSFXSR and OSXMMEXCPT */
    movl %eax, %cr4

    /* Load CR3 from trampoline parameters */
    movl (0x8000 + (smp_boot_cr3 - smp_trampoline_start)), %eax
    movl %eax, %cr3

    /* Enable Long Mode (bit 8 LME) and NXE (bit 11 NXE) in EFER MSR (0xC0000080) */
    movl $0xc0000080, %ecx
    rdmsr
    orl $0x900, %eax
    wrmsr

    /* Enable Paging (CR0.PG = bit 31) and Protection (CR0.PE = bit 0) */
    movl %cr0, %eax
    andl $0xfffffff3, %eax              /* clear CR0.EM and CR0.TS */
    orl $0x80010003, %eax               /* paging, protection and CR0.MP */
    movl %eax, %cr0

    /* Far jump to 64-bit long mode code segment (selector 0x18) */
    ljmpl $0x18, $(0x8000 + (smp_trampoline_lm64 - smp_trampoline_start))

.code64
smp_trampoline_lm64:
    /* Set 64-bit data segment */
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    /* Load 64-bit kernel stack allocated for this AP */
    movq (0x8000 + (smp_boot_stack - smp_trampoline_start)), %rsp
    xorq %rbp, %rbp

    /* Call C entry point */
    movq (0x8000 + (smp_boot_entry - smp_trampoline_start)), %rax
    callq *%rax

.Lhalt64:
    cli
    hlt
    jmp .Lhalt64

/* Trampoline temporary GDT and data */
.align 8
smp_trampoline_gdt:
    .quad 0x0000000000000000            /* 0x00: Null */
    .quad 0x00cf9a000000ffff            /* 0x08: 32-bit Code (R/X) */
    .quad 0x00cf92000000ffff            /* 0x10: 32-bit Data (R/W) */
    .quad 0x00af9a000000ffff            /* 0x18: 64-bit Code (R/X) */
    .quad 0x00cf92000000ffff            /* 0x20: 64-bit Data (R/W) */
smp_trampoline_gdt_end:

.align 4
smp_trampoline_gdt_ptr:
    .word (smp_trampoline_gdt_end - smp_trampoline_gdt - 1)
    .long (0x8000 + (smp_trampoline_gdt - smp_trampoline_start))

.align 8
smp_boot_cr3:
    .quad 0
smp_boot_stack:
    .quad 0
smp_boot_entry:
    .quad 0
smp_boot_status:
    .long 0
    .long 0

smp_trampoline_end:
