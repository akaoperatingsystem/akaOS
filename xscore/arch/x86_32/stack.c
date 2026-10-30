#include <xscore/stack.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096U
#define STACK_REGION_BASE 0x30000000U

static uint64_t boot_stack_top = 0;
static uint64_t ist_stack_tops[2] = {0, 0};
static uint64_t syscall_stack_top = 0;
static uint32_t next_stack_address = STACK_REGION_BASE;
static uint64_t stack_physical_pages[
    XSCORE_BOOT_STACK_PAGES * 2U + XSCORE_IST_STACK_PAGES * 2U];
static uint32_t stack_physical_page_count;

static uint64_t allocate_stack(uint32_t stack_pages) {
    /* Reserve address space for guard page + stack pages */
    uint32_t guard_address = next_stack_address;
    next_stack_address += PAGE_SIZE; /* guard page unmapped */

    uint32_t stack_base = next_stack_address;
    for (uint32_t i = 0; i < stack_pages; ++i) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) return 0;
        uint32_t virtual = stack_base + i * PAGE_SIZE;
        uint64_t flags = XSCORE_PAGE_WRITABLE;
        if (!xscore_vmm_map_page((uint64_t)virtual, physical, flags)) {
            xscore_pmm_free_page(physical);
            return 0;
        }
        if (stack_physical_page_count <
            sizeof(stack_physical_pages) / sizeof(stack_physical_pages[0])) {
            stack_physical_pages[stack_physical_page_count++] = physical;
        }
    }
    next_stack_address += stack_pages * PAGE_SIZE;
    (void)guard_address;

    return (uint64_t)(stack_base + stack_pages * PAGE_SIZE);
}

int xscore_stack_initialize(void) {
    boot_stack_top = allocate_stack(XSCORE_BOOT_STACK_PAGES);
    if (!boot_stack_top) return 0;

    ist_stack_tops[0] = allocate_stack(XSCORE_IST_STACK_PAGES);
    if (!ist_stack_tops[0]) return 0;

    ist_stack_tops[1] = allocate_stack(XSCORE_IST_STACK_PAGES);
    if (!ist_stack_tops[1]) return 0;

    syscall_stack_top = allocate_stack(XSCORE_BOOT_STACK_PAGES);
    if (!syscall_stack_top) return 0;

    return 1;
}

uint64_t xscore_stack_boot_top(void) {
    return boot_stack_top;
}

uint64_t xscore_stack_ist_top(int ist_index) {
    if (ist_index >= 1 && ist_index <= 2) {
        return ist_stack_tops[ist_index - 1];
    }
    return 0;
}

uint64_t xscore_stack_syscall_top(void) {
    return syscall_stack_top;
}

int xscore_stack_owns_physical_page(uint64_t physical_address) {
    if ((physical_address & (PAGE_SIZE - 1U)) != 0) return 0;
    for (uint32_t index = 0; index < stack_physical_page_count; ++index) {
        if (stack_physical_pages[index] == physical_address) return 1;
    }
    return 0;
}
