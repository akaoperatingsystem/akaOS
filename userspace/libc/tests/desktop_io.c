#include <akaos/syscall.h>
#include <akaos/poll.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The desktop compatibility DSOs are deliberately freestanding: they are
 * loaded before the complete libc is available.  Keep this small syscall
 * bridge local to the runtime object instead of making the DSO depend on the
 * process libc.  The argument order mirrors the native akaOS open/read/write
 * ABI and is valid for every supported user architecture.
 */
static long desktop_syscall3(uint64_t number, uint64_t argument0,
                             uint64_t argument1, uint64_t argument2) {
#if defined(__x86_64__)
    register uint64_t result __asm__("rax") = number;
    register uint64_t first __asm__("rdi") = argument0;
    register uint64_t second __asm__("rsi") = argument1;
    register uint64_t third __asm__("rdx") = argument2;
    __asm__ volatile("int $0x80"
                     : "+a"(result)
                     : "D"(first), "S"(second), "d"(third)
                     : "cc", "memory");
    return (long)result;
#elif defined(__i386__)
    register uint32_t result __asm__("eax") = (uint32_t)number;
    register uint32_t first __asm__("ebx") = (uint32_t)argument0;
    register uint32_t second __asm__("ecx") = (uint32_t)argument1;
    register uint32_t third __asm__("edx") = (uint32_t)argument2;
    __asm__ volatile("int $0x80"
                     : "+a"(result)
                     : "b"(first), "c"(second), "d"(third)
                     : "cc", "memory");
    return (long)result;
#elif defined(__aarch64__)
    register uint64_t x8 __asm__("x8") = number;
    register uint64_t x0 __asm__("x0") = argument0;
    register uint64_t x1 __asm__("x1") = argument1;
    register uint64_t x2 __asm__("x2") = argument2;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2)
                     : "memory");
    return (long)x0;
#elif defined(__arm__)
    register uint32_t r7 __asm__("r7") = (uint32_t)number;
    register uint32_t r0 __asm__("r0") = (uint32_t)argument0;
    register uint32_t r1 __asm__("r1") = (uint32_t)argument1;
    register uint32_t r2 __asm__("r2") = (uint32_t)argument2;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2)
                     : "r3", "r12", "cc", "memory");
    return (long)r0;
#elif defined(__riscv)
#if __riscv_xlen == 64
    register uint64_t a7 __asm__("a7") = number;
    register uint64_t a0 __asm__("a0") = argument0;
    register uint64_t a1 __asm__("a1") = argument1;
    register uint64_t a2 __asm__("a2") = argument2;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7), "r"(a1), "r"(a2)
                     : "memory");
    return (long)a0;
#else
    register uint32_t a7 __asm__("a7") = (uint32_t)number;
    register uint32_t a0 __asm__("a0") = (uint32_t)argument0;
    register uint32_t a1 __asm__("a1") = (uint32_t)argument1;
    register uint32_t a2 __asm__("a2") = (uint32_t)argument2;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7), "r"(a1), "r"(a2)
                     : "memory");
    return (long)a0;
#endif
#else
#error "Unsupported architecture for desktop runtime syscall bridge"
#endif
}

long akaos_desktop_open(const char *path, size_t length, int flags) {
    return desktop_syscall3(AKAOS_SYSCALL_OPEN, (uint64_t)(uintptr_t)path,
                            (uint64_t)length, (uint64_t)(unsigned)flags);
}

long akaos_desktop_read(int descriptor, void *buffer, size_t length) {
    return desktop_syscall3(AKAOS_SYSCALL_READ, (uint64_t)(unsigned)descriptor,
                            (uint64_t)(uintptr_t)buffer, (uint64_t)length);
}

long akaos_desktop_getdents(int descriptor, void *entries, size_t capacity) {
    return desktop_syscall3(AKAOS_SYSCALL_GETDENTS,
                            (uint64_t)(unsigned)descriptor,
                            (uint64_t)(uintptr_t)entries,
                            (uint64_t)capacity);
}

long akaos_desktop_stat(const char *path, size_t length, void *status) {
    return desktop_syscall3(AKAOS_SYSCALL_STAT, (uint64_t)(uintptr_t)path,
                            (uint64_t)length, (uint64_t)(uintptr_t)status);
}

long akaos_desktop_write(int descriptor, const void *buffer, size_t length) {
    return desktop_syscall3(AKAOS_SYSCALL_WRITE,
                            (uint64_t)(unsigned)descriptor,
                            (uint64_t)(uintptr_t)buffer, (uint64_t)length);
}

long akaos_desktop_close(int descriptor) {
    return desktop_syscall3(AKAOS_SYSCALL_CLOSE, (uint64_t)(unsigned)descriptor,
                            0U, 0U);
}

/* Keep the GLib-shaped runtime independent of the process libc while still
 * allowing its main context to wait on real akaOS descriptors. */
long akaos_desktop_poll(struct akaos_pollfd *descriptors, size_t count,
                        int timeout_ms) {
    return desktop_syscall3(AKAOS_SYSCALL_POLL,
                            (uint64_t)(uintptr_t)descriptors,
                            (uint64_t)count,
                            (uint64_t)(int64_t)timeout_ms);
}
