#ifndef XSCORE_APIC_H
#define XSCORE_APIC_H

#include <stdint.h>

/*
 * Hardware interrupt vector assignments.
 * Vectors 0–31 are CPU exceptions (already handled).
 * Vectors 32–47 are available for hardware IRQs.
 * Vector 255 is the APIC spurious interrupt.
 */
#define XSCORE_IRQ_BASE        32
#define XSCORE_IRQ_TIMER       32
#define XSCORE_IRQ_SPURIOUS    255
#define XSCORE_IRQ_RESCHEDULE  254
#define XSCORE_IRQ_TLB_SHOOTDOWN 253

/*
 * Disable the legacy 8259 PIC and initialize the Local APIC.
 * Must be called after the IDT is installed and IST stacks are ready.
 * Returns 1 on success, 0 on failure.
 */
int xscore_apic_initialize(void);

/*
 * Send End-Of-Interrupt to the Local APIC.
 * Must be called at the end of every APIC interrupt handler.
 */
void xscore_apic_eoi(void);

/*
 * Read the Local APIC ID for the current processor.
 */
uint32_t xscore_apic_id(void);
/* Route a legacy ISA source to the BSP, respecting MADT overrides. */
int xscore_irq_route_isa(uint8_t irq);

#endif
