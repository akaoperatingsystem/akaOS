#ifndef XSCORE_ARCH_H
#define XSCORE_ARCH_H

#include <stdint.h>

void xscore_gdt_initialize(void);
void xscore_idt_initialize(void);
void xscore_tss_install(void);
void xscore_tss_set_rsp0(uint64_t stack_top);
uint64_t xscore_tss_rsp0(void);
void xscore_idt_install_irq_handlers(void);
void xscore_idt_install_syscall_handler(void);
void xscore_breakpoint_self_test(void);
int xscore_breakpoint_was_handled(void);

struct xscore_user_transition;
extern void xscore_user_enter(uint64_t instruction_pointer, uint64_t stack_pointer,
                              struct xscore_user_transition *transition);
extern void xscore_user_resume(const void *context,
                               struct xscore_user_transition *transition);

void xscore_exception_handler(uint64_t vector, uint64_t error_code, const uint64_t *frame);

#endif
