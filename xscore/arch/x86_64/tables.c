#include <xscore/arch.h>
#include <xscore/apic.h>
#include <xscore/stack.h>
#include <stddef.h>
#include <stdint.h>

struct descriptor_table_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/*
 * Task State Segment for x86-64.
 * Required for IST (Interrupt Stack Table) entries and
 * eventual ring transitions.
 */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

#include <xscore/smp.h>

/*
 * GDT layout:
 *   [0] Null descriptor
 *   [1] Kernel code (0x08)
 *   [2] Kernel data (0x10)
 *   [3] User data (0x18, Ring 3 selector 0x1b)
 *   [4] User code (0x20, Ring 3 selector 0x23)
 *   [5 + 2*cpu] TSS descriptor for core (16 bytes, spans two entries)
 */
#define GDT_TOTAL_ENTRIES (5 + 2 * ACPI_MAX_CPUS)
static uint64_t gdt[GDT_TOTAL_ENTRIES] __attribute__((aligned(16)));
static struct tss per_cpu_tss[ACPI_MAX_CPUS] __attribute__((aligned(16)));
static struct idt_entry idt[256] __attribute__((aligned(16)));
static volatile int breakpoint_handled;

extern void xscore_gdt_load(const struct descriptor_table_pointer *pointer);
extern void xscore_idt_load(const struct descriptor_table_pointer *pointer);
extern void xscore_tss_load(uint16_t selector);
extern void (*xscore_isr_stub_table[32])(void);
/* Hardware IRQ stubs (vectors 32–47) */
extern void xscore_irq_0(void);
extern void xscore_irq_1(void);
extern void xscore_irq_2(void);
extern void xscore_irq_3(void);
extern void xscore_irq_4(void);
extern void xscore_irq_5(void);
extern void xscore_irq_6(void);
extern void xscore_irq_7(void);
extern void xscore_irq_8(void);
extern void xscore_irq_9(void);
extern void xscore_irq_10(void);
extern void xscore_irq_11(void);
extern void xscore_irq_12(void);
extern void xscore_irq_13(void);
extern void xscore_irq_14(void);
extern void xscore_irq_15(void);
extern void xscore_irq_reschedule(void);
extern void xscore_irq_tlb_shootdown(void);
extern void xscore_irq_spurious(void);
extern void xscore_syscall_int80(void);

void xscore_gdt_initialize(void) {
    gdt[0] = UINT64_C(0x0000000000000000); /* Null */
    gdt[1] = UINT64_C(0x00af9a000000ffff); /* Kernel code */
    gdt[2] = UINT64_C(0x00cf92000000ffff); /* Kernel data */
    gdt[3] = UINT64_C(0x00cff2000000ffff); /* User data */
    gdt[4] = UINT64_C(0x00affa000000ffff); /* User code */
    gdt[5] = 0; /* TSS low — filled in by xscore_tss_install */
    gdt[6] = 0; /* TSS high — filled in by xscore_tss_install */

    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };
    xscore_gdt_load(&pointer);
}

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes, uint8_t ist) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08;
    idt[vector].ist = ist;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

void xscore_idt_initialize(void) {
    for (uint8_t vector = 0; vector < 32; ++vector) {
        idt_set_gate(vector, xscore_isr_stub_table[vector], 0x8e, 0);
    }
    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    xscore_idt_load(&pointer);
}

/*
 * Install the TSS into the GDT and configure IST entries for BSP (Core 0).
 * Must be called after kernel stacks are allocated.
 */
void xscore_tss_install(void) {
    /* Zero the BSP TSS */
    uint8_t *tss_bytes = (uint8_t *)&per_cpu_tss[0];
    for (size_t i = 0; i < sizeof(struct tss); ++i) tss_bytes[i] = 0;

    /* Set IST entries from allocated stacks */
    per_cpu_tss[0].ist1 = xscore_stack_ist_top(1); /* Double-fault */
    per_cpu_tss[0].ist2 = xscore_stack_ist_top(2); /* NMI */
    per_cpu_tss[0].rsp0 = xscore_stack_syscall_top();
    per_cpu_tss[0].iopb_offset = (uint16_t)sizeof(struct tss);

    /*
     * Build a 64-bit TSS descriptor (16 bytes, spans gdt[5] and gdt[6]).
     * Format:
     *   gdt[5]: limit[15:0] | base[23:0] | access | flags | limit[19:16] | base[31:24]
     *   gdt[6]: base[63:32] | reserved
     */
    uint64_t base = (uint64_t)(uintptr_t)&per_cpu_tss[0];
    uint32_t limit = (uint32_t)(sizeof(struct tss) - 1);

    gdt[5] = ((base & 0x00FFFFFFULL) << 16)
           | (limit & 0xFFFFULL)
           | (((base >> 24) & 0xFFULL) << 56)
           | (UINT64_C(0x89) << 40)          /* Present, type = 64-bit TSS (available) */
           | ((uint64_t)((limit >> 16) & 0xF) << 48);
    gdt[6] = (base >> 32) & 0xFFFFFFFFULL;

    /* Reload GDT with TSS descriptor included */
    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };
    xscore_gdt_load(&pointer);

    /* Load TSS: selector for gdt[5] = 5 * 8 = 0x28 */
    xscore_tss_load(0x28);

    /* Update IDT: double-fault uses IST1, NMI uses IST2 */
    idt_set_gate(2, xscore_isr_stub_table[2], 0x8e, 2);   /* NMI → IST2 */
    idt_set_gate(8, xscore_isr_stub_table[8], 0x8e, 1);    /* Double-fault → IST1 */

    /* Reload IDT to pick up IST changes */
    const struct descriptor_table_pointer idt_pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    xscore_idt_load(&idt_pointer);
}

void xscore_arch_ap_load_tables(void) {
    const struct descriptor_table_pointer gdt_ptr = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };
    xscore_gdt_load(&gdt_ptr);

    const struct descriptor_table_pointer idt_ptr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    xscore_idt_load(&idt_ptr);
}

void xscore_tss_install_core(uint32_t cpu_index, uint64_t ist1, uint64_t ist2, uint64_t rsp0) {
    if (cpu_index >= ACPI_MAX_CPUS) return;
    struct tss *t = &per_cpu_tss[cpu_index];
    uint8_t *b = (uint8_t *)t;
    for (size_t i = 0; i < sizeof(struct tss); ++i) b[i] = 0;

    t->ist1 = ist1;
    t->ist2 = ist2;
    t->rsp0 = rsp0;
    t->iopb_offset = (uint16_t)sizeof(struct tss);

    uint64_t base = (uint64_t)(uintptr_t)t;
    uint32_t limit = (uint32_t)(sizeof(struct tss) - 1);

    uint32_t slot = 5 + 2 * cpu_index;
    gdt[slot] = ((base & 0x00FFFFFFULL) << 16)
              | (limit & 0xFFFFULL)
              | (((base >> 24) & 0xFFULL) << 56)
              | (UINT64_C(0x89) << 40)          /* Present, type = 64-bit TSS (available) */
              | ((uint64_t)((limit >> 16) & 0xF) << 48);
    gdt[slot + 1] = (base >> 32) & 0xFFFFFFFFULL;

    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };
    xscore_gdt_load(&pointer);

    uint16_t selector = (uint16_t)(slot * 8);
    xscore_tss_load(selector);
}

void xscore_tss_set_rsp0(uint64_t stack_top) {
    uint32_t cpu = xscore_smp_current_cpu_index();
    if (cpu < ACPI_MAX_CPUS && stack_top) {
        per_cpu_tss[cpu].rsp0 = stack_top;
    }
}

uint64_t xscore_tss_rsp0(void) {
    uint32_t cpu = xscore_smp_current_cpu_index();
    if (cpu < ACPI_MAX_CPUS) return per_cpu_tss[cpu].rsp0;
    return 0;
}


void xscore_idt_install_syscall_handler(void) {
    /* Present Ring 3 interrupt gate: user code may invoke int $0x80. */
    idt_set_gate(0x80, xscore_syscall_int80, 0xee, 0);
    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    xscore_idt_load(&pointer);
}

/*
 * Install hardware IRQ handlers into the IDT (vectors 32–47, 255).
 * Must be called after xscore_idt_initialize().
 */
void xscore_idt_install_irq_handlers(void) {
    void (*irq_handlers[16])(void) = {
        xscore_irq_0, xscore_irq_1, xscore_irq_2, xscore_irq_3,
        xscore_irq_4, xscore_irq_5, xscore_irq_6, xscore_irq_7,
        xscore_irq_8, xscore_irq_9, xscore_irq_10, xscore_irq_11,
        xscore_irq_12, xscore_irq_13, xscore_irq_14, xscore_irq_15,
    };
    for (uint8_t i = 0; i < 16; ++i) {
        idt_set_gate(32 + i, irq_handlers[i], 0x8e, 0);
    }
    idt_set_gate(XSCORE_IRQ_RESCHEDULE, xscore_irq_reschedule, 0x8e, 0);
    idt_set_gate(XSCORE_IRQ_TLB_SHOOTDOWN, xscore_irq_tlb_shootdown, 0x8e, 0);
    /* Spurious interrupt vector */
    idt_set_gate(255, xscore_irq_spurious, 0x8e, 0);

    /* Reload IDT */
    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    xscore_idt_load(&pointer);
}

void xscore_breakpoint_self_test(void) {
    breakpoint_handled = 0;
    __asm__ volatile("int3");
}

int xscore_breakpoint_was_handled(void) {
    return breakpoint_handled != 0;
}

void xscore_arch_mark_breakpoint_handled(void) {
    breakpoint_handled = 1;
}
