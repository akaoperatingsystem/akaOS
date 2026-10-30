.global _start
.type _start, @function
.extern exit
.extern __akaos_libc_run_init_array

_start:
    lw a0, 0(sp)
    addi a1, sp, 4
    mv s0, a0
    mv s1, a1
    mv s2, sp
    mv a2, s2
    call __akaos_libc_set_environ
    call __akaos_libc_run_init_array
    mv a0, s0
    mv a1, s1
    mv fp, zero
    mv ra, zero
    call main
    call exit
1:  wfi
    j 1b

.global __akaos_sigreturn_trampoline
.type __akaos_sigreturn_trampoline, @function
__akaos_sigreturn_trampoline:
    li a7, 36
    ecall
1:  wfi
    j 1b
