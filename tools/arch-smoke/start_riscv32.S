.section .text.start,"ax",@progbits
.align 2
.global _start
.type _start,@function
_start:
    call akaos_arch_smoke_main
1:
    wfi
    j 1b
.size _start, .-_start
