#include <xscore/stack.h>
#include <xscore/apic.h>
#include <stdint.h>
#include <stddef.h>

static uint8_t kernel_syscall_stack[8192] __attribute__((aligned(16)));

uint64_t xscore_stack_syscall_top(void) {
    return (uint64_t)(uintptr_t)&kernel_syscall_stack[8192];
}

uint64_t xscore_stack_boot_top(void) {
    return (uint64_t)(uintptr_t)&kernel_syscall_stack[8192];
}

uint64_t xscore_stack_ist_top(int ist_index) {
    (void)ist_index;
    return (uint64_t)(uintptr_t)&kernel_syscall_stack[8192];
}

int xscore_stack_initialize(void) {
    return 1;
}

uint32_t xscore_apic_id(void) {
    return 0;
}

int xscore_apic_initialize(void) {
    return 1;
}

void xscore_apic_eoi(void) {
}

void xscore_tss_set_rsp0(uint64_t stack_top) {
    (void)stack_top;
}

uint64_t xscore_tss_rsp0(void) {
    return 0;
}
