.section .text.start,"ax",@progbits
.global _start
.type _start,@function
_start:
    call akaos_arch_smoke_main
1:
    cli
    hlt
    jmp 1b
.size _start, .-_start
