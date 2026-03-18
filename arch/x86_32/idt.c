/* ============================================================
 * akaOS — Interrupt Descriptor Table Implementation (x86_32)
 * ============================================================
 * 32-bit IDT entries are 8 bytes.
 * Installs CPU exception handlers (ISR 0-31) and IRQ handlers (32-47).
 */

#include "idt.h"
#include "io.h"
#include "string.h"
#include "vga.h"

extern void gui_pump(void);

/* 32-bit IDT entry structure (8 bytes) */
struct idt_entry {
    uint16_t base_low;      /* Offset bits 0-15 */
    uint16_t sel;           /* Code segment selector */
    uint8_t  always0;       /* Always zero */
    uint8_t  flags;         /* Type and attributes */
    uint16_t base_high;     /* Offset bits 16-31 */
} __attribute__((packed));

/* IDT pointer for lidt */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
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

/* Set a single IDT gate (32-bit) */
static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
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

    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

void idt_init(void) {
    memset(&idt, 0, sizeof(struct idt_entry) * 256);

    pic_remap();

    /* Install CPU exception handlers (INT 0-31)
     * 0x08 = kernel code segment, 0x8E = present, ring 0, 32-bit interrupt gate */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Install IRQ handlers (INT 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* Load IDT */
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint32_t)&idt;
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

static volatile int in_fault = 0;

void isr_handler(struct regs *r) {
    int num = (int)r->int_no;

    if (in_fault) {
        asm volatile("cli; hlt");
        __builtin_unreachable();
    }
    in_fault = 1;

    const char *name = (num < 32) ? exception_names[num] : "Unknown";

    extern volatile int gui_mode_active;
    if (gui_mode_active) {
        extern void gui_term_print(const char *s);
        extern void gui_term_set_color(uint32_t fg);
        gui_term_set_color(0xf7768e);
        gui_term_print("\n*** CPU EXCEPTION: ");
        gui_term_print(name);
        gui_term_print(" ***\n");
        char buf[24];
        gui_term_print("  INT#: "); int_to_str(num, buf); gui_term_print(buf); gui_term_print("\n");
        gui_term_print("  Error: "); int_to_str((int)r->err_code, buf); gui_term_print(buf); gui_term_print("\n");
        gui_term_set_color(0xa9b1d6);
        gui_term_print("  System halted. Please reboot.\n");
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

    asm volatile("cli; hlt");
    __builtin_unreachable();
}
