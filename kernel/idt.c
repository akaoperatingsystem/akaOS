/* ============================================================
 * akaOS — Interrupt Descriptor Table Implementation (64-bit)
 * ============================================================
 * 64-bit IDT entries are 16 bytes (vs 8 in 32-bit).
 * Installs CPU exception handlers (ISR 0-31) and IRQ handlers (32-47).
 */

#include "idt.h"
#include "io.h"
#include "string.h"
#include "vga.h"

extern void gui_pump(void);

/* 64-bit IDT entry structure (16 bytes) */
struct idt_entry {
    uint16_t base_low;      /* Offset bits 0-15 */
    uint16_t sel;           /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table offset (0 = none) */
    uint8_t  flags;         /* Type and attributes */
    uint16_t base_mid;      /* Offset bits 16-31 */
    uint32_t base_high;     /* Offset bits 32-63 */
    uint32_t reserved;      /* Must be zero */
} __attribute__((packed));

/* IDT pointer for lidt (64-bit: 10 bytes) */
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* 256 IDT entries */
static struct idt_entry idt[256];
struct idt_ptr idtp;

/* IRQ handler function pointers */
static irq_handler_t irq_handlers[16] = { 0 };

/* External assembly functions — ISR stubs (exceptions) */
extern void isr0(void);  extern void isr1(void);
extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);
extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void);
extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void);
extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void);
extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* External assembly functions — IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);
extern void idt_load(void);

/* Set a single IDT gate (64-bit) */
static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].sel       = sel;
    idt[num].ist       = 0;
    idt[num].flags     = flags;
    idt[num].reserved  = 0;
}

/* Remap the PIC (8259) so IRQs 0-15 map to ISRs 32-47 */
static void pic_remap(void) {
    outb(0x20, 0x11);  io_wait();
    outb(0xA0, 0x11);  io_wait();
    outb(0x21, 0x20);  io_wait();  /* Master: IRQ 0-7  → INT 32-39 */
    outb(0xA1, 0x28);  io_wait();  /* Slave:  IRQ 8-15 → INT 40-47 */
    outb(0x21, 0x04);  io_wait();
    outb(0xA1, 0x02);  io_wait();
    outb(0x21, 0x01);  io_wait();
    outb(0xA1, 0x01);  io_wait();

    /* Set masks: unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade)
     * Master mask: 0xF8 = 11111000 (bits 0,1,2 clear = unmasked)
     * Slave mask:  0xEF = 11101111 (bit 4 clear = IRQ12/mouse unmasked) */
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

void idt_init(void) {
    memset(&idt, 0, sizeof(struct idt_entry) * 256);

    pic_remap();

    /* Install CPU exception handlers (INT 0-31)
     * 0x08 = kernel code segment, 0x8E = present, ring 0, 64-bit interrupt gate */
    idt_set_gate(0,  (uint64_t)isr0,  0x28, 0x8E);
    idt_set_gate(1,  (uint64_t)isr1,  0x28, 0x8E);
    idt_set_gate(2,  (uint64_t)isr2,  0x28, 0x8E);
    idt_set_gate(3,  (uint64_t)isr3,  0x28, 0x8E);
    idt_set_gate(4,  (uint64_t)isr4,  0x28, 0x8E);
    idt_set_gate(5,  (uint64_t)isr5,  0x28, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x28, 0x8E);
    idt_set_gate(7,  (uint64_t)isr7,  0x28, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x28, 0x8E);
    idt_set_gate(9,  (uint64_t)isr9,  0x28, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x28, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x28, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x28, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x28, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x28, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x28, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x28, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x28, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x28, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x28, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x28, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x28, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x28, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x28, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x28, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x28, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x28, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x28, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x28, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x28, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x28, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x28, 0x8E);

    /* Install IRQ handlers (INT 32-47) */
    idt_set_gate(32, (uint64_t)irq0,  0x28, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x28, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x28, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x28, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x28, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x28, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x28, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x28, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x28, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x28, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x28, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x28, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x28, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x28, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x28, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x28, 0x8E);

    /* Load IDT */
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint64_t)&idt;
    idt_load();

    /* Enable interrupts */
    asm volatile ("sti");
}

void irq_install_handler(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

/* Called from assembly irq_common_stub */
void irq_handler(struct regs *r) {
    int irq = (int)(r->int_no - 32);

    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](r);
    }

    if (r->int_no >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

/* Exception names for debug output */
static const char *exception_names[] = {
    "Division By Zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device N/A",
    "Double Fault", "Coproc Seg", "Invalid TSS", "Seg Not Present",
    "Stack Fault", "GPF", "Page Fault", "Reserved",
    "x87 FP", "Alignment Check", "Machine Check", "SIMD FP",
    "Virt Exception", "Rsv","Rsv","Rsv","Rsv","Rsv","Rsv","Rsv","Rsv","Rsv",
    "Security", "Rsv"
};

/* Flag to track if we're already handling a fault (prevent recursion) */
static volatile int in_fault = 0;

/* Called from assembly isr_common_stub for CPU exceptions */
void isr_handler(struct regs *r) {
    int num = (int)r->int_no;

    /* Prevent recursive faults */
    if (in_fault) {
        asm volatile("cli; hlt");
        __builtin_unreachable();
    }
    in_fault = 1;

    /* Print a diagnostic message */
    const char *name = (num < 32) ? exception_names[num] : "Unknown";

    /* Print to the GUI terminal if in GUI mode */
    extern volatile int gui_mode_active;
    if (gui_mode_active) {
        extern void gui_term_print(const char *s);
        extern void gui_term_set_color(uint32_t fg);
        gui_term_set_color(0xf7768e); /* red */
        gui_term_print("\n*** CPU EXCEPTION: ");
        gui_term_print(name);
        gui_term_print(" ***\n");
        char buf[24];
        gui_term_print("  INT#: "); int_to_str(num, buf); gui_term_print(buf); gui_term_print("\n");
        gui_term_print("  Error: "); int_to_str((int)r->err_code, buf); gui_term_print(buf); gui_term_print("\n");
        gui_term_set_color(0xa9b1d6); /* restore */
        gui_term_print("  System halted. Please reboot.\n");
        /* Render one last frame so the user sees the message */
        gui_pump();
    } else {
        vga_print_color("\n*** CPU EXCEPTION: ", VGA_LIGHT_RED, VGA_BLACK);
        vga_print_color(name, VGA_WHITE, VGA_BLACK);
        vga_print_color(" ***\n", VGA_LIGHT_RED, VGA_BLACK);
        char buf[24];
        vga_print("  INT#: "); int_to_str(num, buf); vga_print(buf); vga_print("\n");
        vga_print("  Error: "); int_to_str((int)r->err_code, buf); vga_print(buf); vga_print("\n");
        vga_print("  System halted. Please reboot.\n");
    }

    /* ALL CPU faults halt the system. Returning would retry the faulting
     * instruction, causing an infinite fault loop (freeze). */
    asm volatile("cli; hlt");
    __builtin_unreachable();
}

