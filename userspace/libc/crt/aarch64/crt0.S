.global _start
.type _start, %function
.extern exit
.extern __akaos_libc_run_init_array

_start:
    ldr x0, [sp]
    add x1, sp, #8
    mov x19, x0
    mov x20, x1
    mov x21, sp
    mov x2, x21
    bl __akaos_libc_set_environ
    bl __akaos_libc_run_init_array
    mov x0, x19
    mov x1, x20
    mov x29, #0
    mov x30, #0
    bl main
    bl exit
1:  wfe
    b 1b

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, %function
__akaos_sigreturn_trampoline:
    mov x8, #36
    svc #0
1:  wfe
    b 1b
