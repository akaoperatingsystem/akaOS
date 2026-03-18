/* ============================================================
 * akaOS — AArch64 Interrupt Controller (GIC Stub)
 * ============================================================
 * Provides the same API as x86 IDT (idt_init, irq_install_handler)
 * but backed by ARM's GIC (Generic Interrupt Controller).
 * Currently a minimal stub — real GIC configuration would depend
 * on the specific SoC or QEMU virt machine's GIC version.
 */

#include "idt.h"
#include "string.h"
#include "vga.h"

extern void gui_pump(void);

/* IRQ handler function pointers — same API as x86 */
static irq_handler_t irq_handlers[16] = { 0 };

/* Exception vector table (defined in interrupts.S) */
extern char vectors[];

void idt_init(void) {
    /* Install the exception vector table */
    asm volatile("msr vbar_el1, %0" :: "r"(vectors));

    /* TODO: Initialize GICv2/GICv3 distributor and CPU interface
     * For now this is a stub — interrupts are enabled but no
     * hardware IRQ sources are configured */

    /* Enable interrupts */
    asm volatile("msr daifclr, #2");
}

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16)
        irq_handlers[irq] = handler;
}

/* Called from assembly vector table for IRQ exceptions */
void aarch64_irq_handler(void *frame) {
    (void)frame;
    /* TODO: Read GIC IAR to determine which IRQ fired,
     * call the appropriate handler, then write EOI */
}

/* Called from assembly vector table for synchronous exceptions */
void aarch64_sync_handler(void *frame) {
    (void)frame;

    /* Read the exception syndrome register */
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));

    uint32_t ec = (uint32_t)((esr >> 26) & 0x3F);

    extern volatile int gui_mode_active;
    if (gui_mode_active) {
        extern void gui_term_print(const char *s);
        extern void gui_term_set_color(uint32_t fg);
        gui_term_set_color(0xf7768e);
        gui_term_print("\n*** ARM64 EXCEPTION ***\n");
        char buf[24];
        gui_term_print("  ESR EC: "); int_to_str((int)ec, buf); gui_term_print(buf); gui_term_print("\n");
        gui_term_set_color(0xa9b1d6);
        gui_term_print("  System halted. Please reboot.\n");
        gui_pump();
    } else {
        vga_print_color("\n*** ARM64 EXCEPTION ***\n", VGA_LIGHT_RED, VGA_BLACK);
        char buf[24];
        vga_print("  ESR EC: "); int_to_str((int)ec, buf); vga_print(buf); vga_print("\n");
        vga_print("  System halted. Please reboot.\n");
    }

    asm volatile("msr daifset, #2");
    while (1) asm volatile("wfi");
    __builtin_unreachable();
}

/* Stub: Called from irq_common_stub on x86. No-op on ARM64. */
void irq_handler(struct regs *r) { (void)r; }
void isr_handler(struct regs *r) { (void)r; }
