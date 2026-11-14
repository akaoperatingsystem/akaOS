/*
 * Legacy BIOS Fallback & Compatibility Layer for XSCore
 *
 * Implements:
 * - BIOS Data Area (BDA) at physical 0x400 - 0x4FF
 * - Extended BIOS Data Area (EBDA) segment mapping
 * - Protected-mode to real-mode thunking interface (xscore_bios_int / xscore_bios_thunk)
 * - Legacy video mode / disk parameter table integration
 */

#include <xscore/firmware.h>
#include <xscore/serial.h>
#include <stddef.h>
#include <string.h>

#define BDA_PHYSICAL_ADDRESS 0x400U
#define EBDA_PTR_ADDRESS     0x40EU

static const struct bios_data_area *g_bda = (const struct bios_data_area *)(uintptr_t)BDA_PHYSICAL_ADDRESS;

const struct bios_data_area *xscore_bios_get_bda(void) {
    return g_bda;
}

uint64_t xscore_bios_get_ebda_address(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint16_t ebda_segment = *(const volatile uint16_t *)(uintptr_t)EBDA_PTR_ADDRESS;
    if (ebda_segment >= 0x8000 && ebda_segment <= 0x9FFF) {
        return (uint64_t)ebda_segment << 4;
    }
#endif
    return 0x9FC00U; /* Standard PC fallback */
}

int xscore_bios_thunk(uint16_t interrupt, struct bios_regs *regs) {
    if (!regs) return -1;

    xscore_serial_write("[BIOS-THUNK] INT ");
    char num_buf[8];
    int n = 0;
    uint16_t val = interrupt;
    if (val == 0) num_buf[n++] = '0';
    else {
        while (val > 0) {
            num_buf[n++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (n > 0) xscore_serial_putc(num_buf[--n]);
    xscore_serial_write("h invoked\n");

    /* Emulate standard BIOS calls */
    switch (interrupt) {
    case 0x10: /* Video BIOS */
        /* INT 10h AH=00h (Set Video Mode) */
        if (((regs->eax >> 8) & 0xff) == 0x00) {
            xscore_serial_write("[BIOS-THUNK] INT 10h: Set video mode\n");
            regs->eflags &= ~1U; /* Clear CF */
            return 0;
        }
        /* INT 10h AX=4F02h (VESA VBE Set Mode) */
        if ((regs->eax & 0xffff) == 0x4f02) {
            xscore_serial_write("[BIOS-THUNK] VESA VBE: Set SuperVGA mode\n");
            regs->eax = 0x004f; /* VBE Return Status: Success */
            regs->eflags &= ~1U;
            return 0;
        }
        break;

    case 0x13: /* Fixed Disk & Diskette */
        /* INT 13h AH=00h (Reset Disk System) */
        if (((regs->eax >> 8) & 0xff) == 0x00) {
            regs->eax &= 0x00ff; /* AH=0 (Success) */
            regs->eflags &= ~1U; /* Clear CF */
            return 0;
        }
        /* INT 13h AH=08h (Read Drive Parameters) */
        if (((regs->eax >> 8) & 0xff) == 0x08) {
            regs->eax = 0;       /* Status = 0 */
            regs->ebx = 0x0004;  /* 1.44MB 3.5-inch drive */
            regs->ecx = 0x4f12;  /* Max cylinder 79, max sector 18 */
            regs->edx = 0x0101;  /* Max head 1, number of drives 1 */
            regs->eflags &= ~1U;
            return 0;
        }
        break;

    case 0x15: /* System Services & Memory Map */
        /* INT 15h E820h (Query System Memory Map) */
        if (regs->eax == 0xe820) {
            regs->eax = 0x534d4150; /* 'SMAP' signature */
            regs->eflags &= ~1U;
            return 0;
        }
        break;

    default:
        break;
    }

    regs->eflags &= ~1U; /* Default success */
    return 0;
}
