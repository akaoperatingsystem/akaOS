#ifndef AKAOS_ARCHITECTURE_H
#define AKAOS_ARCHITECTURE_H

/*
 * Shared architecture identification for the SDK and native userspace.
 *
 * Keep this header compiler-derived rather than tied to the build host. The
 * same source can therefore be checked with the six target triples supported
 * by tools/build-arch-smoke.sh.
 */
#define AKAOS_ARCH_X86_64 1
#define AKAOS_ARCH_X86_32 2
#define AKAOS_ARCH_AARCH64 3
#define AKAOS_ARCH_ARM32 4
#define AKAOS_ARCH_RISCV64 5
#define AKAOS_ARCH_RISCV32 6

#if defined(__x86_64__) || defined(_M_X64)
#define AKAOS_ARCH_ID AKAOS_ARCH_X86_64
#define AKAOS_ARCH_NAME "x86_64"
#define AKAOS_ARCH_BITS 64
#elif defined(__i386__) || defined(_M_IX86)
#define AKAOS_ARCH_ID AKAOS_ARCH_X86_32
#define AKAOS_ARCH_NAME "x86_32"
#define AKAOS_ARCH_BITS 32
#elif defined(__aarch64__) || defined(_M_ARM64)
#define AKAOS_ARCH_ID AKAOS_ARCH_AARCH64
#define AKAOS_ARCH_NAME "aarch64"
#define AKAOS_ARCH_BITS 64
#elif defined(__arm__) || defined(_M_ARM)
#define AKAOS_ARCH_ID AKAOS_ARCH_ARM32
#define AKAOS_ARCH_NAME "arm32"
#define AKAOS_ARCH_BITS 32
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
#define AKAOS_ARCH_ID AKAOS_ARCH_RISCV64
#define AKAOS_ARCH_NAME "riscv64"
#define AKAOS_ARCH_BITS 64
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32)
#define AKAOS_ARCH_ID AKAOS_ARCH_RISCV32
#define AKAOS_ARCH_NAME "riscv32"
#define AKAOS_ARCH_BITS 32
#else
#error "Unsupported akaOS architecture: add compiler detection to architecture.h"
#endif

#endif
