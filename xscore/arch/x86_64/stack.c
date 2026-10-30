#include <xscore/stack.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)

/*
 * Kernel stacks are mapped in the high-half region above the heap.
 * Each stack has a guard page below it (unmapped → page fault on overflow).
 *
 * Layout for each stack (low to high):
 *   [guard page - unmapped]
 *   [stack pages - RW, NX]
 *   RSP starts at top of stack pages
 */
#define STACK_REGION_BASE UINT64_C(0xffff810000000000)

#include <xscore/smp.h>

static uint64_t boot_stack_top;
static uint64_t ist_stack_tops[2]; /* IST1 = double-fault, IST2 = NMI */
static uint64_t syscall_stack_top;
static uint64_t next_stack_address = STACK_REGION_BASE;
static xscore_spinlock_t stack_lock = XSCORE_SPINLOCK_INIT;

uint64_t xscore_stack_allocate(uint32_t stack_pages) {
    xscore_spin_lock(&stack_lock);
    /* Reserve address space for guard page + stack pages */
    uint64_t guard_address = next_stack_address;
    next_stack_address += PAGE_SIZE; /* guard page: just skip, leave unmapped */

    uint64_t stack_base = next_stack_address;
    next_stack_address += stack_pages * PAGE_SIZE;
    xscore_spin_unlock(&stack_lock);

    for (uint32_t i = 0; i < stack_pages; ++i) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) return 0;
        uint64_t virtual = stack_base + i * PAGE_SIZE;
        uint64_t flags = XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE;
        if (!xscore_vmm_map_page(virtual, physical, flags)) {
            xscore_pmm_free_page(physical);
            return 0;
        }
    }
    (void)guard_address;

    /* Stack grows downward: top is base + size */
    return stack_base + stack_pages * PAGE_SIZE;
}

int xscore_stack_initialize(void) {
    /* Allocate boot stack: 16 KiB with guard page */
    boot_stack_top = xscore_stack_allocate(XSCORE_BOOT_STACK_PAGES);
    if (!boot_stack_top) return 0;

    /* IST1: double-fault stack (8 KiB with guard page) */
    ist_stack_tops[0] = xscore_stack_allocate(XSCORE_IST_STACK_PAGES);
    if (!ist_stack_tops[0]) return 0;

    /* IST2: NMI stack (8 KiB with guard page) */
    ist_stack_tops[1] = xscore_stack_allocate(XSCORE_IST_STACK_PAGES);
    if (!ist_stack_tops[1]) return 0;

    /* Ring 3 interrupts and system calls enter XSCore on this stack. */
    syscall_stack_top = xscore_stack_allocate(XSCORE_BOOT_STACK_PAGES);
    if (!syscall_stack_top) return 0;

    /*
     * The actual transition onto the boot stack is performed by
     * xscore_stack_enter() from the caller, which jumps into a fresh
     * function on the new stack. Switching RSP inside a normal C
     * function is unsafe: the compiler-owned epilogue and the caller's
     * frame live on the old stack.
     */
    return 1;
}

uint64_t xscore_stack_boot_top(void) {
    return boot_stack_top;
}

uint64_t xscore_stack_syscall_top(void) {
    return syscall_stack_top;
}

uint64_t xscore_stack_ist_top(int ist_index) {
    if (ist_index < 1 || ist_index > 2) return 0;
    return ist_stack_tops[ist_index - 1];
}
