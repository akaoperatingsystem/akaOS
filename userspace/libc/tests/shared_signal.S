/* Signal-return trampoline exported by the shared libc image.
 *
 * Static programs get the same entry point from their architecture-specific
 * crt0.  Keeping a copy in libc.so.6 means signal wrappers remain usable when
 * an executable resolves libc through PT_INTERP instead of carrying libc in
 * its own image.
 */

#if defined(__x86_64__)
.text
.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    movq $36, %rax
    int $0x80
1:
    pause
    jmp 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
#elif defined(__i386__)
.text
.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    movl $36, %eax
    int $0x80
1:
    pause
    jmp 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
#elif defined(__aarch64__)
.text
.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, %function
__akaos_sigreturn_trampoline:
    mov x8, #36
    svc #0
1:
    wfe
    b 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
#elif defined(__arm__)
.text
.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, %function
__akaos_sigreturn_trampoline:
    mov r7, #36
    svc #0
1:
    wfi
    b 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
#elif defined(__riscv)
.text
.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    li a7, 36
    ecall
1:
    wfi
    j 1b
.size __akaos_sigreturn_trampoline, .-__akaos_sigreturn_trampoline
#endif
