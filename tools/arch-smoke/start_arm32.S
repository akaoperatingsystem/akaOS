.syntax unified
.cpu cortex-a7
.arm
.section .text.start,"ax",%progbits
.align 2
.global _start
.type _start,%function
_start:
    bl akaos_arch_smoke_main
1:
    wfe
    b 1b
.size _start, .-_start
