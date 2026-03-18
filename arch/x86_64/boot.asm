; ============================================================
; akaOS 64-bit Boot — Limine Protocol Entry
; ============================================================

section .bss
align 16
stack_bottom: resb 65536
stack_top:

section .text
BITS 64
global _start
extern kernel_main

_start:
    ; The Limine protocol starts us in 64-bit mode with:
    ; CS: 64-bit code segment
    ; SS, DS, ES: 64-bit data segment
    ; RDI: (In some cases, but we use the requests instead)

    ; Switch to our internal stack
    mov rsp, stack_top
    
    ; Clear RDI (we'll use Limine requests in C to get info)
    xor rdi, rdi
    
    call kernel_main

    cli
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
