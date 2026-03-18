// ============================================================
// akaOS — AArch64 Boot Entry (Limine Protocol)
// ============================================================

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text
.global _start
.extern kernel_main

_start:
    // Limine starts us in EL1 with MMU on (identity or HHDM mapped)
    // Set up our own stack
    adrp x0, stack_top
    add  x0, x0, :lo12:stack_top
    mov  sp, x0

    // Clear x0 (no arguments for kernel_main via Limine requests)
    mov  x0, #0

    bl   kernel_main

    // If kernel_main returns, halt
    msr  daifset, #2       // Disable IRQs
1:
    wfi
    b    1b

// Suppress executable stack
.section .note.GNU-stack, "", %progbits
