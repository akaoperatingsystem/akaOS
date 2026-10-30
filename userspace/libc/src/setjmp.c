#include <setjmp.h>

#if defined(__x86_64__)

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "movq %rbx, 0(%rdi)\n\t"
        "movq %rbp, 8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 48(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 56(%rdi)\n\t"
        "xorl %eax, %eax\n\t"
        "retq");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "movq %rbx, 0(%rdi)\n\t"
        "movq %rbp, 8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 48(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 56(%rdi)\n\t"
        "xorl %eax, %eax\n\t"
        "retq");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "movq %rdi, %r8\n\t"
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n"
        "1:\n\t"
        "movq 0(%r8), %rbx\n\t"
        "movq 8(%r8), %rbp\n\t"
        "movq 16(%r8), %r12\n\t"
        "movq 24(%r8), %r13\n\t"
        "movq 32(%r8), %r14\n\t"
        "movq 40(%r8), %r15\n\t"
        "movq 48(%r8), %rsp\n\t"
        "jmp *56(%r8)");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "movq %rdi, %r8\n\t"
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n"
        "1:\n\t"
        "movq 0(%r8), %rbx\n\t"
        "movq 8(%r8), %rbp\n\t"
        "movq 16(%r8), %r12\n\t"
        "movq 24(%r8), %r13\n\t"
        "movq 32(%r8), %r14\n\t"
        "movq 40(%r8), %r15\n\t"
        "movq 48(%r8), %rsp\n\t"
        "jmp *56(%r8)");
}

#elif defined(__i386__)

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "movl 4(%esp), %edx\n\t"
        "movl %ebx, 0(%edx)\n\t"
        "movl %esi, 4(%edx)\n\t"
        "movl %edi, 8(%edx)\n\t"
        "movl %ebp, 12(%edx)\n\t"
        "leal 4(%esp), %eax\n\t"
        "movl %eax, 16(%edx)\n\t"
        "movl (%esp), %eax\n\t"
        "movl %eax, 20(%edx)\n\t"
        "xorl %eax, %eax\n\t"
        "retl");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "movl 4(%esp), %edx\n\t"
        "movl %ebx, 0(%edx)\n\t"
        "movl %esi, 4(%edx)\n\t"
        "movl %edi, 8(%edx)\n\t"
        "movl %ebp, 12(%edx)\n\t"
        "leal 4(%esp), %eax\n\t"
        "movl %eax, 16(%edx)\n\t"
        "movl (%esp), %eax\n\t"
        "movl %eax, 20(%edx)\n\t"
        "xorl %eax, %eax\n\t"
        "retl");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "movl 4(%esp), %edx\n\t"
        "movl 8(%esp), %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n"
        "1:\n\t"
        "movl 0(%edx), %ebx\n\t"
        "movl 4(%edx), %esi\n\t"
        "movl 8(%edx), %edi\n\t"
        "movl 12(%edx), %ebp\n\t"
        "movl 16(%edx), %esp\n\t"
        "jmp *20(%edx)");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "movl 4(%esp), %edx\n\t"
        "movl 8(%esp), %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n"
        "1:\n\t"
        "movl 0(%edx), %ebx\n\t"
        "movl 4(%edx), %esi\n\t"
        "movl 8(%edx), %edi\n\t"
        "movl 12(%edx), %ebp\n\t"
        "movl 16(%edx), %esp\n\t"
        "jmp *20(%edx)");
}

#elif defined(__aarch64__)

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "stp x19, x20, [x0, #0]\n\t"
        "stp x21, x22, [x0, #16]\n\t"
        "stp x23, x24, [x0, #32]\n\t"
        "stp x25, x26, [x0, #48]\n\t"
        "stp x27, x28, [x0, #64]\n\t"
        "str x29, [x0, #80]\n\t"
        "str x30, [x0, #88]\n\t"
        "mov x9, sp\n\t"
        "str x9, [x0, #96]\n\t"
        "stp d8, d9, [x0, #104]\n\t"
        "stp d10, d11, [x0, #120]\n\t"
        "stp d12, d13, [x0, #136]\n\t"
        "stp d14, d15, [x0, #152]\n\t"
        "mov w0, #0\n\t"
        "ret");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "stp x19, x20, [x0, #0]\n\t"
        "stp x21, x22, [x0, #16]\n\t"
        "stp x23, x24, [x0, #32]\n\t"
        "stp x25, x26, [x0, #48]\n\t"
        "stp x27, x28, [x0, #64]\n\t"
        "str x29, [x0, #80]\n\t"
        "str x30, [x0, #88]\n\t"
        "mov x9, sp\n\t"
        "str x9, [x0, #96]\n\t"
        "stp d8, d9, [x0, #104]\n\t"
        "stp d10, d11, [x0, #120]\n\t"
        "stp d12, d13, [x0, #136]\n\t"
        "stp d14, d15, [x0, #152]\n\t"
        "mov w0, #0\n\t"
        "ret");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mov x9, x0\n\t"
        "mov w0, w1\n\t"
        "cbnz w0, 1f\n\t"
        "mov w0, #1\n"
        "1:\n\t"
        "ldp x19, x20, [x9, #0]\n\t"
        "ldp x21, x22, [x9, #16]\n\t"
        "ldp x23, x24, [x9, #32]\n\t"
        "ldp x25, x26, [x9, #48]\n\t"
        "ldp x27, x28, [x9, #64]\n\t"
        "ldr x29, [x9, #80]\n\t"
        "ldr x30, [x9, #88]\n\t"
        "ldp d8, d9, [x9, #104]\n\t"
        "ldp d10, d11, [x9, #120]\n\t"
        "ldp d12, d13, [x9, #136]\n\t"
        "ldp d14, d15, [x9, #152]\n\t"
        "ldr x10, [x9, #96]\n\t"
        "mov sp, x10\n\t"
        "br x30");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mov x9, x0\n\t"
        "mov w0, w1\n\t"
        "cbnz w0, 1f\n\t"
        "mov w0, #1\n"
        "1:\n\t"
        "ldp x19, x20, [x9, #0]\n\t"
        "ldp x21, x22, [x9, #16]\n\t"
        "ldp x23, x24, [x9, #32]\n\t"
        "ldp x25, x26, [x9, #48]\n\t"
        "ldp x27, x28, [x9, #64]\n\t"
        "ldr x29, [x9, #80]\n\t"
        "ldr x30, [x9, #88]\n\t"
        "ldp d8, d9, [x9, #104]\n\t"
        "ldp d10, d11, [x9, #120]\n\t"
        "ldp d12, d13, [x9, #136]\n\t"
        "ldp d14, d15, [x9, #152]\n\t"
        "ldr x10, [x9, #96]\n\t"
        "mov sp, x10\n\t"
        "br x30");
}

#elif defined(__arm__)

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "stmia r0, {r4-r11}\n\t"
        "str sp, [r0, #32]\n\t"
        "str lr, [r0, #36]\n\t"
        "mov r0, #0\n\t"
        "bx lr");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "stmia r0, {r4-r11}\n\t"
        "str sp, [r0, #32]\n\t"
        "str lr, [r0, #36]\n\t"
        "mov r0, #0\n\t"
        "bx lr");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mov r2, r0\n\t"
        "mov r0, r1\n\t"
        "cmp r0, #0\n\t"
        "moveq r0, #1\n\t"
        "ldr r3, [r2, #32]\n\t"
        "ldr lr, [r2, #36]\n\t"
        "ldmia r2, {r4-r11}\n\t"
        "mov sp, r3\n\t"
        "bx lr");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mov r2, r0\n\t"
        "mov r0, r1\n\t"
        "cmp r0, #0\n\t"
        "moveq r0, #1\n\t"
        "ldr r3, [r2, #32]\n\t"
        "ldr lr, [r2, #36]\n\t"
        "ldmia r2, {r4-r11}\n\t"
        "mov sp, r3\n\t"
        "bx lr");
}

#elif defined(__riscv) && __riscv_xlen == 64

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "sd s0, 0(a0)\n\t"
        "sd s1, 8(a0)\n\t"
        "sd s2, 16(a0)\n\t"
        "sd s3, 24(a0)\n\t"
        "sd s4, 32(a0)\n\t"
        "sd s5, 40(a0)\n\t"
        "sd s6, 48(a0)\n\t"
        "sd s7, 56(a0)\n\t"
        "sd s8, 64(a0)\n\t"
        "sd s9, 72(a0)\n\t"
        "sd s10, 80(a0)\n\t"
        "sd s11, 88(a0)\n\t"
        "sd sp, 96(a0)\n\t"
        "sd ra, 104(a0)\n\t"
        "li a0, 0\n\t"
        "ret");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "sd s0, 0(a0)\n\t"
        "sd s1, 8(a0)\n\t"
        "sd s2, 16(a0)\n\t"
        "sd s3, 24(a0)\n\t"
        "sd s4, 32(a0)\n\t"
        "sd s5, 40(a0)\n\t"
        "sd s6, 48(a0)\n\t"
        "sd s7, 56(a0)\n\t"
        "sd s8, 64(a0)\n\t"
        "sd s9, 72(a0)\n\t"
        "sd s10, 80(a0)\n\t"
        "sd s11, 88(a0)\n\t"
        "sd sp, 96(a0)\n\t"
        "sd ra, 104(a0)\n\t"
        "li a0, 0\n\t"
        "ret");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mv t0, a0\n\t"
        "mv a0, a1\n\t"
        "bnez a0, 1f\n\t"
        "li a0, 1\n"
        "1:\n\t"
        "ld s0, 0(t0)\n\t"
        "ld s1, 8(t0)\n\t"
        "ld s2, 16(t0)\n\t"
        "ld s3, 24(t0)\n\t"
        "ld s4, 32(t0)\n\t"
        "ld s5, 40(t0)\n\t"
        "ld s6, 48(t0)\n\t"
        "ld s7, 56(t0)\n\t"
        "ld s8, 64(t0)\n\t"
        "ld s9, 72(t0)\n\t"
        "ld s10, 80(t0)\n\t"
        "ld s11, 88(t0)\n\t"
        "ld sp, 96(t0)\n\t"
        "ld t1, 104(t0)\n\t"
        "jr t1");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mv t0, a0\n\t"
        "mv a0, a1\n\t"
        "bnez a0, 1f\n\t"
        "li a0, 1\n"
        "1:\n\t"
        "ld s0, 0(t0)\n\t"
        "ld s1, 8(t0)\n\t"
        "ld s2, 16(t0)\n\t"
        "ld s3, 24(t0)\n\t"
        "ld s4, 32(t0)\n\t"
        "ld s5, 40(t0)\n\t"
        "ld s6, 48(t0)\n\t"
        "ld s7, 56(t0)\n\t"
        "ld s8, 64(t0)\n\t"
        "ld s9, 72(t0)\n\t"
        "ld s10, 80(t0)\n\t"
        "ld s11, 88(t0)\n\t"
        "ld sp, 96(t0)\n\t"
        "ld t1, 104(t0)\n\t"
        "jr t1");
}

#elif defined(__riscv) && __riscv_xlen == 32

__attribute__((naked, returns_twice))
int setjmp(jmp_buf environment) {
    __asm__ volatile(
        "sw s0, 0(a0)\n\t"
        "sw s1, 4(a0)\n\t"
        "sw s2, 8(a0)\n\t"
        "sw s3, 12(a0)\n\t"
        "sw s4, 16(a0)\n\t"
        "sw s5, 20(a0)\n\t"
        "sw s6, 24(a0)\n\t"
        "sw s7, 28(a0)\n\t"
        "sw s8, 32(a0)\n\t"
        "sw s9, 36(a0)\n\t"
        "sw s10, 40(a0)\n\t"
        "sw s11, 44(a0)\n\t"
        "sw sp, 48(a0)\n\t"
        "sw ra, 52(a0)\n\t"
        "li a0, 0\n\t"
        "ret");
}

__attribute__((naked, returns_twice))
int _setjmp(jmp_buf environment) {
    __asm__ volatile(
        "sw s0, 0(a0)\n\t"
        "sw s1, 4(a0)\n\t"
        "sw s2, 8(a0)\n\t"
        "sw s3, 12(a0)\n\t"
        "sw s4, 16(a0)\n\t"
        "sw s5, 20(a0)\n\t"
        "sw s6, 24(a0)\n\t"
        "sw s7, 28(a0)\n\t"
        "sw s8, 32(a0)\n\t"
        "sw s9, 36(a0)\n\t"
        "sw s10, 40(a0)\n\t"
        "sw s11, 44(a0)\n\t"
        "sw sp, 48(a0)\n\t"
        "sw ra, 52(a0)\n\t"
        "li a0, 0\n\t"
        "ret");
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mv t0, a0\n\t"
        "mv a0, a1\n\t"
        "bnez a0, 1f\n\t"
        "li a0, 1\n"
        "1:\n\t"
        "lw s0, 0(t0)\n\t"
        "lw s1, 4(t0)\n\t"
        "lw s2, 8(t0)\n\t"
        "lw s3, 12(t0)\n\t"
        "lw s4, 16(t0)\n\t"
        "lw s5, 20(t0)\n\t"
        "lw s6, 24(t0)\n\t"
        "lw s7, 28(t0)\n\t"
        "lw s8, 32(t0)\n\t"
        "lw s9, 36(t0)\n\t"
        "lw s10, 40(t0)\n\t"
        "lw s11, 44(t0)\n\t"
        "lw sp, 48(t0)\n\t"
        "lw t1, 52(t0)\n\t"
        "jr t1");
}

__attribute__((naked, noreturn))
void _longjmp(jmp_buf environment, int value) {
    __asm__ volatile(
        "mv t0, a0\n\t"
        "mv a0, a1\n\t"
        "bnez a0, 1f\n\t"
        "li a0, 1\n"
        "1:\n\t"
        "lw s0, 0(t0)\n\t"
        "lw s1, 4(t0)\n\t"
        "lw s2, 8(t0)\n\t"
        "lw s3, 12(t0)\n\t"
        "lw s4, 16(t0)\n\t"
        "lw s5, 20(t0)\n\t"
        "lw s6, 24(t0)\n\t"
        "lw s7, 28(t0)\n\t"
        "lw s8, 32(t0)\n\t"
        "lw s9, 36(t0)\n\t"
        "lw s10, 40(t0)\n\t"
        "lw s11, 44(t0)\n\t"
        "lw sp, 48(t0)\n\t"
        "lw t1, 52(t0)\n\t"
        "jr t1");
}

#else
#error "Unsupported akaOS setjmp architecture"
#endif
