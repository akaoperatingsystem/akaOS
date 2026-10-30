.section .text.start,"ax",@progbits
.global _start
.type _start, @function
.extern main
.extern _exit
.extern __akaos_libc_set_environ
.extern __akaos_libc_run_init_array

/* PT_INTERP i386 entry point.  Unlike the static CRT, this variant must set
 * EBX to the executable's GOT base before calling PLT entries supplied by
 * libc.so.6. */
_start:
    xorl %ebp, %ebp
    call __x86.get_pc_thunk.bx
    addl $_GLOBAL_OFFSET_TABLE_, %ebx

    movl (%esp), %edi        /* argc */
    leal 4(%esp), %esi       /* argv pointer */
    movl %esp, %edx          /* original initial stack pointer */
    andl $-16, %esp
    subl $4, %esp

    pushl %edx
    pushl %esi
    pushl %edi
    call __akaos_libc_set_environ@PLT
    addl $16, %esp

    /* Initialize libc state before entering stack-protected application code. */
    call __akaos_libc_run_init_array@PLT

    pushl %esi
    pushl %edi
    call main

    pushl %eax
    call _exit@PLT
1:
    pause
    jmp 1b
.size _start, .-_start

.hidden __x86.get_pc_thunk.bx
.type __x86.get_pc_thunk.bx, @function
__x86.get_pc_thunk.bx:
    movl (%esp), %ebx
    ret
.size __x86.get_pc_thunk.bx, .-__x86.get_pc_thunk.bx

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    movl $36, %eax
    int $0x80
1:
    pause
    jmp 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
