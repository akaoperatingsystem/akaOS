#include <stdint.h>
#include <xscore/tty.h>
#include <stddef.h>

struct gdt_entry_32 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr_32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry_32 {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

struct idt_entry_32 {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr_32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry_32 gdt[7];
static struct gdt_ptr_32 gdt_descriptor;
static struct tss_entry_32 tss;

static struct idt_entry_32 idt[256];
static struct idt_ptr_32 idt_descriptor;

extern void xscore_32_load_gdt(uint32_t);
extern void xscore_32_load_idt(uint32_t);
extern void xscore_32_load_tss(uint16_t);
extern void xscore_32_outb(uint16_t port, uint8_t val);
extern uint8_t xscore_32_inb(uint16_t port);

/* Extern declarations for ISR stubs from entry.S */
extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void);
extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void);
extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void); extern void irq1(void); extern void irq2(void); extern void irq3(void);
extern void irq4(void); extern void irq5(void); extern void irq6(void); extern void irq7(void);
extern void irq8(void); extern void irq9(void); extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

extern void syscall_entry_32(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high = (uint8_t)((base >> 24) & 0xFF);

    gdt[num].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[num].granularity |= (uint8_t)(gran & 0xF0);
    gdt[num].access = access;
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (uint16_t)(base & 0xFFFF);
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
}

static volatile int breakpoint_handled = 0;

void xscore_arch_mark_breakpoint_handled(void) {
    breakpoint_handled = 1;
}

void xscore_breakpoint_self_test(void) {
    breakpoint_handled = 0;
    __asm__ volatile("int3");
}

int xscore_breakpoint_was_handled(void) {
    return breakpoint_handled;
}

void xscore_32_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

void xscore_tss_set_rsp0(uint64_t stack_top) {
    tss.esp0 = (uint32_t)stack_top;
}

uint64_t xscore_tss_rsp0(void) {
    return (uint64_t)tss.esp0;
}

void xscore_gdt_initialize(void) {
    /* Set up GDT:
     * 0: Null
     * 1: Kernel Code (0x08)
     * 2: Kernel Data (0x10)
     * 3: User Data   (0x18 | 3 = 0x1B)
     * 4: User Code   (0x20 | 3 = 0x23)
     * 5: TSS         (0x28)
     * 6: User TLS    (0x30 | RPL3 = 0x33, GS base selected per thread)
     */
    gdt_set_gate(0, 0, 0, 0, 0);                /* Null */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel Code (0x08) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel Data (0x10) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User Data (0x18 | 3) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User Code (0x20 | 3) */
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User TLS data (0x33) */

    for (size_t i = 0; i < sizeof(tss); ++i) ((uint8_t*)&tss)[i] = 0;
    tss.ss0 = 0x10;
    tss.esp0 = 0;
    tss.iomap_base = sizeof(tss);
    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00); /* TSS (0x28) */

    gdt_descriptor.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_descriptor.base = (uint32_t)&gdt;
    xscore_32_load_gdt((uint32_t)&gdt_descriptor);
}

/* Update the per-thread GS descriptor used by the i386 static-TLS ABI.  The
 * return paths reload selector 0x33 after this write so the hidden segment
 * cache observes the new base without disturbing the kernel data segments. */
void xscore_32_set_user_tls_base(uint32_t base) {
    gdt_set_gate(6, base, 0xFFFFFFFF, 0xF2, 0xCF);
}

void xscore_tss_install(void) {
    xscore_32_load_tss(0x28);
}

void xscore_idt_initialize(void) {
    for (int i = 0; i < 256; ++i) idt_set_gate((uint8_t)i, 0, 0, 0);

    /* Exception vectors 0-31 */
    void (*isrs[32])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    for (int i = 0; i < 32; ++i) {
        idt_set_gate((uint8_t)i, (uint32_t)isrs[i], 0x08, 0x8E);
    }

    idt_descriptor.limit = (uint16_t)(sizeof(idt) - 1);
    idt_descriptor.base = (uint32_t)&idt;
    xscore_32_load_idt((uint32_t)&idt_descriptor);
}

void xscore_idt_install_irq_handlers(void) {
    /* Remap 8259 PIC IRQs to vectors 32..47 */
    xscore_32_outb(0x20, 0x11);
    xscore_32_outb(0xA0, 0x11);
    xscore_32_outb(0x21, 0x20); /* Master PIC offset = 32 */
    xscore_32_outb(0xA1, 0x28); /* Slave PIC offset = 40 */
    xscore_32_outb(0x21, 0x04);
    xscore_32_outb(0xA1, 0x02);
    xscore_32_outb(0x21, 0x01);
    xscore_32_outb(0xA1, 0x01);
    xscore_32_outb(0x21, 0x00); /* Unmask all master IRQs */
    xscore_32_outb(0xA1, 0x00); /* Unmask all slave IRQs */

    void (*irqs[16])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    };
    for (int i = 0; i < 16; ++i) {
        idt_set_gate((uint8_t)(32 + i), (uint32_t)irqs[i], 0x08, 0x8E);
    }
}

void xscore_idt_install_syscall_handler(void) {
    /* Syscall gate at 0x80 (128) with DPL=3 (0xEE) */
    idt_set_gate(0x80, (uint32_t)syscall_entry_32, 0x08, 0xEE);
}

struct xscore_32_trap_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, user_esp, user_ss;
};

extern void xscore_timer_interrupt(void);
extern void xscore_tty_poll_keyboard(void);
extern int xscore_process_deliver_pending_signal(uint64_t *irq_frame);
extern void xscore_thread_preemption_checkpoint(void);
extern void xscore_apic_eoi(void);
extern void xscore_exception_handler(uint64_t vector, uint64_t error_code, const void *frame);

void xscore_32_trap_handler(struct xscore_32_trap_frame *frame) {
    if (frame->int_no == 3) {
        xscore_arch_mark_breakpoint_handled();
        return;
    }
    if (frame->int_no < 32) {
        /* Normalize the 32-bit trap frame to the common exception logger's
         * six-word layout. Passing the native frame as uint64_t[] combines
         * adjacent 32-bit registers and reports bogus EIP/RSP values. */
        uint64_t normalized_frame[6] = {
            frame->int_no,
            frame->err_code,
            frame->eip,
            frame->cs,
            frame->eflags,
            (frame->cs & 3U) ? frame->user_esp :
                (uint64_t)(uintptr_t)frame +
                    offsetof(struct xscore_32_trap_frame, user_esp)
        };
        xscore_exception_handler((uint64_t)frame->int_no,
                                 (uint64_t)frame->err_code,
                                 normalized_frame);
        return;
    }
    if (frame->int_no == 32) {
        xscore_timer_interrupt();
        /* A task switch may suspend this handler. Release the PIC first so
         * the next task can receive timer and device interrupts. */
        xscore_apic_eoi();
        if (!xscore_tty_interrupts_enabled()) xscore_tty_poll_keyboard();
        if ((frame->cs & 3) != 0) {
            if (xscore_process_deliver_pending_signal((uint64_t*)frame)) {
                return;
            }
            xscore_thread_preemption_checkpoint();
        }
        return;
    }
    if (frame->int_no == 33 || frame->int_no == 36 || frame->int_no == 44) {
        /* Timer polling may already have drained the controller before IRQ1
         * is dispatched. Check the controller status through the shared poll
         * path instead of unconditionally rereading stale port 0x60 data. */
        xscore_tty_poll_keyboard();
        xscore_apic_eoi();
        return;
    }
    xscore_apic_eoi();
}

void xscore_32_tables_init(uint32_t kernel_stack_top) {
    xscore_gdt_initialize();
    xscore_tss_set_rsp0(kernel_stack_top);
    xscore_tss_install();
    xscore_idt_initialize();
    xscore_idt_install_irq_handlers();
    xscore_idt_install_syscall_handler();
}
