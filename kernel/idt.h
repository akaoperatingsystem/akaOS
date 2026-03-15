/* ============================================================
 * akaOS — Interrupt Descriptor Table Header (64-bit)
 * ============================================================ */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

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

/* Initialize the IDT and remap the PIC */
void idt_init(void);

/* Install an IRQ handler */
typedef void (*irq_handler_t)(struct regs *r);
void irq_install_handler(int irq, irq_handler_t handler);

#endif /* IDT_H */
