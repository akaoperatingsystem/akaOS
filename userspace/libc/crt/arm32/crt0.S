.syntax unified
.arm
.global _start
.type _start, %function
.extern exit
.extern __akaos_libc_run_init_array

_start:
    /* The kernel supplies the allocated user stack in r2 while entering
     * User mode; install it before reading argc/argv from the stack. */
    mov sp, r2
    mov r6, sp
    ldr r0, [sp]
    add r1, sp, #4
    mov r4, r0
    mov r5, r1
    mov r2, r6
    bl __akaos_libc_set_environ
    bl __akaos_libc_run_init_array
    mov r0, r4
    mov r1, r5
    mov fp, #0
    mov lr, #0
    bl main
    bl exit
1:  wfi
    b 1b

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, %function
__akaos_sigreturn_trampoline:
    mov r7, #36
    svc #0
1:  wfi
    b 1b
