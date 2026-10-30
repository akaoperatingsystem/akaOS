#include <xscore/device_irq.h>
#include <xscore/vmm.h>
#include <xscore/tty.h>
#include <xscore/net.h>
#include <xscore/serial.h>
#include <xscore/timer.h>
#include <xscore/smp.h>
#include <xscore/secondary.h>
#include <xscore/sched.h>
#include <stdint.h>
static int ready;

static void arm_timer(void) {
    uint32_t period = xscore_timer_frequency() / 100;
    if (!period) period = 1;
#if defined(__aarch64__)
    __asm__ volatile("msr cntp_tval_el0, %0; msr cntp_ctl_el0, %1; isb"
                     :: "r"((uint64_t)period), "r"(UINT64_C(1)) : "memory");
#elif defined(__arm__)
    uint32_t control = 1;
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0; mcr p15, 0, %1, c14, c2, 1; isb"
                     :: "r"(period), "r"(control) : "memory");
#elif defined(__riscv)
    uint64_t deadline = xscore_timer_ticks() + period;
    register uintptr_t a0 __asm__("a0") = (uintptr_t)deadline;
    register uintptr_t a1 __asm__("a1") = (uintptr_t)(deadline >> 32);
    register uintptr_t a6 __asm__("a6") = 0;
    register uintptr_t a7 __asm__("a7") = 0x54494d45;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
#endif
}

void xscore_device_irq_wait(void) {
    if (!ready) return;
    uintptr_t saved = xscore_irq_save();
#if defined(__aarch64__)
    /* A nested IRQ overwrites ELR/SPSR before its vector executes. Preserve
     * the outer syscall's return state before temporarily unmasking IRQs. */
    uint64_t return_pc, return_status;
    __asm__ volatile("mrs %0, elr_el1; mrs %1, spsr_el1"
                     : "=r"(return_pc), "=r"(return_status));
    __asm__ volatile("msr daifclr, #2; isb; wfi; msr daifset, #2" ::: "memory");
    __asm__ volatile("msr elr_el1, %0; msr spsr_el1, %1"
                     :: "r"(return_pc), "r"(return_status) : "memory");
#elif defined(__arm__)
    __asm__ volatile("cpsie i; isb; wfi; cpsid i" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("csrsi sstatus, 2; wfi; csrci sstatus, 2" ::: "memory");
#endif
    xscore_irq_restore(saved);
}

static volatile uint32_t *reg32(uintptr_t address) {
    return (volatile uint32_t *)address;
}

static int map_registers(uintptr_t base) {
    if (xscore_vmm_is_mapped(base, 0)) return 1;
    return xscore_vmm_map_page(base, base, XSCORE_PAGE_WRITABLE);
}

#if defined(__aarch64__) || defined(__arm__)
#define GICD UINT32_C(0x08000000)
#define GICC UINT32_C(0x08010000)
static void enable_irq(uint32_t irq) {
    *reg32(GICD + 0x80 + (irq / 32) * 4) |= 1U << (irq % 32);
    *(volatile uint8_t *)(uintptr_t)(GICD + 0x400 + irq) = 0x80;
    *(volatile uint8_t *)(uintptr_t)(GICD + 0x800 + irq) = 1;
    *reg32(GICD + 0x100 + (irq / 32) * 4) = 1U << (irq % 32);
}
#elif defined(__riscv)
static uintptr_t plic_enable_base(void) {
    /* QEMU virt has machine/supervisor contexts in 0x100-byte pairs. */
    return UINT32_C(0x0c002080) +
           (uintptr_t)xscore_secondary_raw_cpu_id() * UINT32_C(0x100);
}

static uintptr_t plic_context_base(void) {
    /* Supervisor claim/complete windows are 0x2000 apart per hart. */
    return UINT32_C(0x0c201000) +
           (uintptr_t)xscore_secondary_raw_cpu_id() * UINT32_C(0x2000);
}
#endif

void xscore_device_irq_initialize(void) {
#if defined(__aarch64__) || defined(__arm__)
    if (!map_registers(GICD) || !map_registers(GICC)) return;
    enable_irq(30); /* Generic physical timer: bounds waits even with no I/O. */
    enable_irq(33); /* PL011 UART */
    for (uint32_t slot = 0; slot < 32; ++slot) {
        uintptr_t base = 0x0a000000U + slot * 0x200U;
        if (!map_registers(base & ~(uintptr_t)4095)) return;
        if (*reg32(base) == 0x74726976 && *reg32(base + 8)) enable_irq(48 + slot);
    }
    *reg32(GICC + 4) = 0xff;
    *reg32(GICC) = 7; /* AckCtl allows Group 1 acknowledgement through IAR. */
    *reg32(GICD) = 3;
    *reg32(0x09000038) = (1U << 4) | (1U << 6);
    __asm__ volatile("dsb sy; isb" ::: "memory");
#elif defined(__riscv)
    uintptr_t enable = plic_enable_base();
    uintptr_t context = plic_context_base();
    if (!map_registers(0x0c000000) ||
        !map_registers(enable & ~(uintptr_t)4095) ||
        !map_registers(context)) return;
    for (uint32_t irq = 1; irq <= 10; ++irq) *reg32(0x0c000000 + irq * 4) = 1;
    *reg32(enable) = 0x5fe; /* VirtIO 1..8 and UART 10 for this hart. */
    *reg32(context) = 0;
    *(volatile uint8_t *)0x10000001 = 1;
    __asm__ volatile("fence iorw, iorw; csrs sie, %0" :: "r"(((uintptr_t)1 << 9) | (1U << 5)) : "memory");
#endif
    arm_timer();
    ready = 1;
    xscore_serial_write("[INFO] Native device interrupt controller enabled\n");
}

void xscore_device_irq_secondary_initialize(void) {
#if defined(__aarch64__) || defined(__arm__)
    /* GIC CPU-interface registers are banked per core.  The distributor and
     * IRQ priorities were configured by the BSP; only the local interface
     * and timer source need to be owned here. */
    enable_irq(30);
    *reg32(GICC + 4) = 0xff;
    *reg32(GICC) = 7;
    arm_timer();
#if defined(__aarch64__)
    __asm__ volatile("dsb sy; isb; msr daifclr, #2; isb" ::: "memory");
#else
    __asm__ volatile("dsb sy; isb; cpsie i" ::: "memory");
#endif
#elif defined(__riscv)
    uintptr_t enable = plic_enable_base();
    uintptr_t context = plic_context_base();
    *reg32(enable) = 0x5fe;
    *reg32(context) = 0;
    arm_timer();
    __asm__ volatile("fence iorw, iorw; csrs sie, %0; csrsi sstatus, 2"
                     :: "r"(((uintptr_t)1 << 9) | (1U << 5)) : "memory");
#else
    return;
#endif
}

void xscore_device_irq_dispatch(void) {
#if defined(__aarch64__) || defined(__arm__)
    uint32_t token = *reg32(GICC + 0xc), irq = token & 1023;
    if (irq >= 1020) return;
    if (irq == 30) {
        arm_timer();
        xscore_sched_timer_tick();
        *reg32(GICC + 0x10) = token;
        return;
    }
    uintptr_t virtio = irq >= 48 && irq < 80 ? 0x0a000000U + (irq - 48) * 0x200U : 0;
    int uart = irq == 33;
#elif defined(__riscv)
    uintptr_t cause;
    __asm__ volatile("csrr %0, scause" : "=r"(cause));
    if ((cause & 0xff) == 5) {
        arm_timer();
        xscore_sched_timer_tick();
        return;
    }
    uintptr_t context = plic_context_base();
    uint32_t token = *reg32(context + 4), irq = token;
    if (!irq) return;
    uintptr_t virtio = irq >= 1 && irq <= 8 ? 0x10001000U + (irq - 1) * 0x1000U : 0;
    int uart = irq == 10;
#endif
    if (virtio) {
        uint32_t status = *reg32(virtio + 0x60);
        *reg32(virtio + 0x64) = status;
        if (*reg32(virtio + 8) == 1) xscore_virtio_net_interrupt();
    }
    if (uart || (virtio && *reg32(virtio + 8) == 18)) xscore_tty_poll_keyboard();
#if defined(__aarch64__) || defined(__arm__)
    if (uart) *reg32(0x09000044) = (1U << 4) | (1U << 6);
    __asm__ volatile("dsb sy" ::: "memory");
    *reg32(GICC + 0x10) = token;
#elif defined(__riscv)
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    *reg32(context + 4) = token;
#endif
    static int seen;
    if (!seen) {
        seen = 1;
        xscore_serial_write("[INFO] Native device hardware interrupt received\n");
    }
}
