#ifndef XSCORE_STACK_H
#define XSCORE_STACK_H

#include <stdint.h>

/*
 * Kernel stack sizes.
 * Boot stack: 16 KiB (4 pages).
 * IST stacks: 8 KiB (2 pages) each for double-fault and NMI.
 * Each stack has a guard page (unmapped) below it.
 */
#define XSCORE_BOOT_STACK_PAGES   4
#define XSCORE_IST_STACK_PAGES    2
#define XSCORE_STACK_GUARD_PAGES  1

/*
 * Allocate the kernel boot stack and IST stacks.
 * Must be called after PMM and VMM are initialized.
 * Returns 1 on success, 0 on failure.
 */
int xscore_stack_initialize(void);

/*
 * Allocate a dedicated kernel stack with guard pages.
 * Returns the stack top virtual address, or 0 on allocation failure.
 */
uint64_t xscore_stack_allocate(uint32_t stack_pages);

/*
 * Query the IST stack top addresses for use in the TSS.
 * ist_index: 1 = double-fault, 2 = NMI.
 * Returns the stack top virtual address, or 0 if not allocated.
 */
uint64_t xscore_stack_ist_top(int ist_index);

/*
 * Query the top of the dedicated kernel boot stack.
 * Valid after xscore_stack_initialize() succeeds.
 */
uint64_t xscore_stack_boot_top(void);

/* Dedicated privilege-transition stack used through TSS.RSP0. */
uint64_t xscore_stack_syscall_top(void);
int xscore_stack_owns_physical_page(uint64_t physical_address);

/*
 * Switch RSP to a dedicated kernel stack and start `target` there.
 * Never returns. Runs only on the new stack from now on; the old stack
 * (the UEFI loader stack) is abandoned. Interrupts must be disabled.
 * Implemented in assembly (entry.S).
 *   stack_top: new stack top, 16-byte aligned
 *   target:    function that must never return
 *   arg:       argument forwarded to target in RDI
 */
extern __attribute__((noreturn)) void xscore_stack_enter(uint64_t stack_top,
                                                         void (*target)(uint64_t),
                                                         uint64_t arg);

#endif
