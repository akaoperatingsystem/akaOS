#ifndef XSCORE_DEVICE_IRQ_H
#define XSCORE_DEVICE_IRQ_H
/* QEMU virt platform: GICv2 on ARM, PLIC supervisor context on RISC-V. */
void xscore_device_irq_initialize(void);
void xscore_device_irq_secondary_initialize(void);
void xscore_device_irq_dispatch(void);
void xscore_device_irq_wait(void);
#endif
