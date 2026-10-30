#include <xscore/apic.h>
#include <xscore/serial.h>
#include <xscore/vmm.h>
#include <xscore/acpi.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Local APIC register offsets (from APIC base address).
 * The APIC is memory-mapped; each register is 32 bits at a 16-byte stride.
 */
#define APIC_REG_ID              0x020
#define APIC_REG_VERSION         0x030
#define APIC_REG_TPR             0x080
#define APIC_REG_EOI             0x0B0
#define APIC_REG_SPURIOUS        0x0F0
#define APIC_REG_LVT_TIMER      0x320
#define APIC_REG_TIMER_INITIAL   0x380
#define APIC_REG_TIMER_CURRENT   0x390
#define APIC_REG_TIMER_DIVIDE    0x3E0

/* APIC spurious vector register: bit 8 enables the APIC */
#define APIC_SPURIOUS_ENABLE     (1U << 8)

/* IA32_APIC_BASE MSR */
#define MSR_APIC_BASE            0x1B
#define APIC_BASE_ENABLE         (1ULL << 11)
#define APIC_BASE_ADDRESS_MASK   0xFFFFF000ULL

/* 8259 PIC ports */
#define PIC1_DATA   0x21
#define PIC2_DATA   0xA1

/* APIC mapped virtual address (identity-mapped at physical address) */
volatile uint32_t *apic_base;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static void apic_write(uint32_t offset, uint32_t value) {
    apic_base[offset / 4] = value;
}

static uint32_t apic_read(uint32_t offset) {
    return apic_base[offset / 4];
}

static void pic_disable(void) {
    /*
     * Mask all IRQs on both 8259 PIC chips.
     * This prevents legacy PIC interrupts from firing after
     * we switch to the Local APIC.
     */
    out8(PIC1_DATA, 0xFF);
    out8(PIC2_DATA, 0xFF);
}

int xscore_apic_initialize(void) {
    /* Step 1: Disable legacy 8259 PIC */
    pic_disable();

    /* Step 2: Read APIC base address from MSR */
    uint64_t apic_msr = rdmsr(MSR_APIC_BASE);
    uint64_t apic_physical = apic_msr & APIC_BASE_ADDRESS_MASK;

    /* Enable APIC in MSR if not already enabled */
    if (!(apic_msr & APIC_BASE_ENABLE)) {
        apic_msr |= APIC_BASE_ENABLE;
        wrmsr(MSR_APIC_BASE, apic_msr);
    }

    /*
     * The identity map covers physical memory up to the framebuffer end,
     * which on current boot configurations (RAM below 4 GiB + framebuffer
     * at ~0x80000000) does NOT include the Local APIC MMIO page at
     * 0xFEE00000. Map it explicitly before touching any APIC register.
     * MMIO is mapped as writable and never executable.
     */
    uint64_t mapped_flags = 0;
    if (!xscore_vmm_is_mapped(apic_physical, &mapped_flags)) {
        if (!xscore_vmm_map_page(apic_physical, apic_physical,
                                 XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE)) {
            return 0;
        }
        xscore_serial_write("[INFO] Local APIC MMIO mapped\n");
    }
    apic_base = (volatile uint32_t *)(uintptr_t)apic_physical;

    /* Step 3: Enable APIC via spurious interrupt vector register */
    apic_write(APIC_REG_SPURIOUS,
               APIC_SPURIOUS_ENABLE | XSCORE_IRQ_SPURIOUS);

    /* Step 4: Set task priority to 0 (accept all interrupts) */
    apic_write(APIC_REG_TPR, 0);

    return 1;
}

void xscore_apic_eoi(void) {
    apic_write(APIC_REG_EOI, 0);
}

uint32_t xscore_apic_id(void) {
    return apic_read(APIC_REG_ID) >> 24;
}

int xscore_irq_route_isa(uint8_t irq) {
    const struct xscore_acpi_info *info = xscore_acpi_get_info();
    if (!info || !info->madt_present || irq >= 16 || irq == 0) return 0;
    uint32_t gsi = irq, flags = 0;
    for (uint32_t i = 0; i < info->iso_count; ++i) {
        if (info->iso_source[i] == irq) {
            gsi = info->iso_gsi[i];
            flags = info->iso_flags[i];
        }
    }
    if ((flags & 3) == 2 || ((flags >> 2) & 3) == 2) return 0;
    for (uint32_t i = 0; i < info->ioapic_count; ++i) {
        uint64_t base = info->ioapic_addresses[i];
        if (!base || (base & 4095)) continue;
        if (!xscore_vmm_is_mapped(base, NULL) &&
            !xscore_vmm_map_page(base, base, XSCORE_PAGE_WRITABLE |
                                 XSCORE_PAGE_NO_EXECUTE | UINT64_C(0x18))) return 0;
        volatile uint32_t *select = (volatile uint32_t *)(uintptr_t)base;
        volatile uint32_t *window = select + 4;
        uintptr_t saved = xscore_irq_save();
        *select = 1;
        uint32_t max_pin = (*window >> 16) & 255;
        uint32_t first = info->ioapic_gsi_bases[i];
        if (gsi < first || gsi - first > max_pin) {
            xscore_irq_restore(saved);
            continue;
        }
        uint32_t reg = 0x10 + 2 * (gsi - first);
        uint32_t low = XSCORE_IRQ_BASE + irq;
        if ((flags & 3) == 3) low |= 1U << 13;
        if (((flags >> 2) & 3) == 3) low |= 1U << 15;
        *select = reg;
        *window = low | (1U << 16);
        *select = reg + 1;
        *window = xscore_apic_id() << 24;
        *select = reg;
        *window = low;
        xscore_irq_restore(saved);
        return 1;
    }
    return 0;
}
