.section .text.start,"ax",@progbits
.global _start
.type _start, @function
.extern main
.extern exit
.extern __akaos_libc_set_environ
.extern __akaos_libc_run_init_array

_start:
    xorq %rbp, %rbp
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    movq %rdi, %r12
    movq %rsi, %r13
    movq %rsp, %rdx
    call __akaos_libc_set_environ
    call __akaos_libc_run_init_array
    movq %r12, %rdi
    movq %r13, %rsi
    call main
    movl %eax, %edi
    call exit
1:
    pause
    jmp 1b
.size _start, .-_start

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    movq $36, %rax
    int $0x80
1:  pause
    jmp 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
