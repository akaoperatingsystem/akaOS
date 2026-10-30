.section .text.start,"ax",@progbits
.global _start
.type _start, @function
.extern main
.extern exit
.extern __akaos_libc_set_environ
.extern __akaos_libc_run_init_array

_start:
    xorl %ebp, %ebp
    /* On entry, stack contains:
       0(%esp) = argc
       4(%esp) = argv[0]
       8(%esp) = argv[1]
       ...
       NULL
    */
    movl (%esp), %eax        /* argc */
    leal 4(%esp), %edx       /* argv pointer */
    movl %esp, %ecx          /* original initial stack pointer */
    movl %eax, %ebx
    movl %edx, %esi

    /* Align stack to 16 bytes for System V ABI */
    andl $-16, %esp
    subl $4, %esp            /* padding for three arguments */

    pushl %ecx               /* initial stack pointer */
    pushl %esi               /* argv */
    pushl %ebx               /* argc */
    call __akaos_libc_set_environ
    addl $16, %esp

    subl $8, %esp
    call __akaos_libc_run_init_array
    addl $8, %esp

    pushl %esi               /* argv */
    pushl %ebx               /* argc */
    call main

    /* Call exit(eax) so C and C++ finalizers run. */
    pushl %eax
    call exit

1:
    pause
    jmp 1b
.size _start, .-_start

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    movl $36, %eax
    int $0x80
1:  pause
    jmp 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
