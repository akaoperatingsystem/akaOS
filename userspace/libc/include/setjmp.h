#ifndef AKAOS_LIBC_SETJMP_H
#define AKAOS_LIBC_SETJMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each target stores the ABI callee-saved registers, stack pointer, and return
 * address in the native jmp_buf layout implemented by libc's setjmp.c. */
#if defined(__x86_64__)
#define AKAOS_JMP_BUF_WORDS 8U
#elif defined(__i386__)
#define AKAOS_JMP_BUF_WORDS 6U
#elif defined(__aarch64__)
#define AKAOS_JMP_BUF_WORDS 21U
#elif defined(__arm__)
#define AKAOS_JMP_BUF_WORDS 10U
#elif defined(__riscv)
#define AKAOS_JMP_BUF_WORDS 14U
#else
#error "akaOS setjmp is not implemented for this architecture"
#endif
typedef uintptr_t jmp_buf[AKAOS_JMP_BUF_WORDS];

int setjmp(jmp_buf environment) __attribute__((returns_twice));
int _setjmp(jmp_buf environment) __attribute__((returns_twice));

void longjmp(jmp_buf environment, int value) __attribute__((noreturn));
void _longjmp(jmp_buf environment, int value) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
