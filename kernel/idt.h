/* ============================================================
 * akaOS — Interrupt Descriptor Table Header (Multi-Arch)
 * ============================================================ */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include "arch.h"

#if defined(ARCH_X86_64)

/* Registers saved by ISR stub (64-bit, no pusha) */
struct regs {
    /* Pushed by irq_common_stub (reverse order) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by IRQ stub */
    uint64_t int_no, err_code;
    /* Pushed by CPU on interrupt */
    uint64_t rip, cs, rflags, rsp, ss;
};

#elif defined(ARCH_X86_32)

/* Registers saved by ISR stub (32-bit, pusha) */
struct regs {
    /* Pushed by stub code */
    uint32_t gs, fs, es, ds;
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    /* Pushed by stub */
    uint32_t int_no, err_code;
    /* Pushed by CPU on interrupt */
    uint32_t eip, cs, eflags, useresp, ss;
};

#elif defined(ARCH_AARCH64)

/* ARM64 exception frame (saved by vector entry macro) */
struct regs {
    uint64_t x[31];         /* x0-x30 */
    uint64_t elr_el1;       /* Exception Link Register */
    uint64_t spsr_el1;      /* Saved Program Status Register */
    uint64_t int_no;        /* Software-set interrupt number */
    uint64_t err_code;      /* Software-set error code */
};

#else
#error "Unknown architecture — define ARCH_X86_64, ARCH_X86_32, or ARCH_AARCH64"
#endif

/* Initialize the IDT/exception vectors and interrupt controller */
void idt_init(void);

/* Install an IRQ handler */
typedef void (*irq_handler_t)(struct regs *r);
void irq_install_handler(int irq, irq_handler_t handler);

#endif /* IDT_H */
