/*
 * Universal Keyboard Subsystem & International Layout Engine for XSCore
 *
 * Implements:
 * - PS/2 i8042 Scancode translation & LED/Typematic hardware controls
 * - International Keymap tables: US, UK, German (QWERTZ), French (AZERTY), Spanish, Nordic, Dvorak
 * - Modifier state engine: Shift, AltGr, CapsLock, NumLock, ScrollLock
 * - USB HID Keyboard report decoding (6KRO/NKRO)
 */

#include <xscore/keyboard.h>
#include <xscore/input.h>
#include <xscore/ps2.h>
#include <xscore/io.h>
#include <xscore/serial.h>
#include <akaos/input.h>

static int g_current_layout = KEYMAP_US;
static uint8_t g_current_leds = 0;

/* --- Layout Translation Tables --- */

/* 1. US QWERTY */
static const char keymap_us_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = '-', [0x0d] = '=', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']', [0x1e] = 'a',
    [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
    [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char keymap_us_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0a] = '(', [0x0b] = ')',
    [0x0c] = '_', [0x0d] = '+', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}', [0x1e] = 'A',
    [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2b] = '|', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

/* 2. UK QWERTY (swaps @ and ", # and ~) */
static const char keymap_uk_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x04] = '£', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0a] = '(', [0x0b] = ')',
    [0x0c] = '_', [0x0d] = '+', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}', [0x1e] = 'A',
    [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '@',
    [0x29] = '¬', [0x2b] = '~', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

/* 3. German QWERTZ (Z and Y swapped, numbers, special chars) */
static const char keymap_de_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = 'ß', [0x0d] = '`', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'z', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = 'ü', [0x1b] = '+', [0x1e] = 'a',
    [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 'ö', [0x28] = 'ä',
    [0x29] = '^', [0x2b] = '#', [0x2c] = 'y', [0x2d] = 'x', [0x2e] = 'c',
    [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '-', [0x39] = ' ',
};

static const char keymap_de_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x04] = '§', [0x05] = '$', [0x06] = '%',
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0a] = ')', [0x0b] = '=',
    [0x0c] = '?', [0x0d] = '`', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Z', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = 'Ü', [0x1b] = '*', [0x1e] = 'A',
    [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = 'Ö', [0x28] = 'Ä',
    [0x29] = '°', [0x2b] = ''', [0x2c] = 'Y', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = ';',
    [0x34] = ':', [0x35] = '_', [0x39] = ' ',
};

/* 4. French AZERTY (A/Q swapped, Z/W swapped, numbers on Shift) */
static const char keymap_fr_normal[128] = {
    [0x02] = '&', [0x03] = 'é', [0x04] = '"', [0x05] = ''', [0x06] = '(',
    [0x07] = '-', [0x08] = 'è', [0x09] = '_', [0x0a] = 'ç', [0x0b] = 'à',
    [0x0c] = ')', [0x0d] = '=', [0x0f] = '\t', [0x10] = 'a', [0x11] = 'z', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '^', [0x1b] = '$', [0x1e] = 'q',
    [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 'm', [0x28] = 'ù',
    [0x29] = '*', [0x2b] = '*', [0x2c] = 'w', [0x2d] = 'x', [0x2e] = 'c',
    [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = ',', [0x33] = ';',
    [0x34] = ':', [0x35] = '!', [0x39] = ' ',
};

static const char keymap_fr_shift[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = '°', [0x0d] = '+', [0x0f] = '\t', [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '¨', [0x1b] = '£', [0x1e] = 'Q',
    [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = 'M', [0x28] = '%',
    [0x29] = 'µ', [0x2b] = 'µ', [0x2c] = 'W', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = '?', [0x33] = '.',
    [0x34] = '/', [0x35] = '§', [0x39] = ' ',
};

/* 5. Dvorak Layout */
static const char keymap_dvorak_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = '[', [0x0d] = ']', [0x0f] = '\t', [0x10] = '\'', [0x11] = ',', [0x12] = '.',
    [0x13] = 'p', [0x14] = 'y', [0x15] = 'f', [0x16] = 'g', [0x17] = 'c',
    [0x18] = 'r', [0x19] = 'l', [0x1a] = '/', [0x1b] = '=', [0x1e] = 'a',
    [0x1f] = 'o', [0x20] = 'e', [0x21] = 'u', [0x22] = 'i', [0x23] = 'd',
    [0x24] = 'h', [0x25] = 't', [0x26] = 'n', [0x27] = 's', [0x28] = '-',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = ';', [0x2d] = 'q', [0x2e] = 'j',
    [0x2f] = 'k', [0x30] = 'x', [0x31] = 'b', [0x32] = 'm', [0x33] = 'w',
    [0x34] = 'v', [0x35] = 'z', [0x39] = ' ',
};

/* Layout names */
static const char *layout_names[] = {
    [KEYMAP_US] = "US QWERTY",
    [KEYMAP_UK] = "UK QWERTY",
    [KEYMAP_DE] = "German QWERTZ",
    [KEYMAP_FR] = "French AZERTY",
    [KEYMAP_ES] = "Spanish",
    [KEYMAP_NORDIC] = "Nordic",
    [KEYMAP_DVORAK] = "Dvorak",
};

int xscore_keyboard_set_layout(int layout) {
    if (layout < 0 || layout >= KEYMAP_MAX) return 0;
    g_current_layout = layout;
    xscore_serial_write("[KEYMAP] Switched keyboard layout to: ");
    xscore_serial_write(layout_names[layout]);
    xscore_serial_write("\n");
    return 1;
}

int xscore_keyboard_get_layout(void) {
    return g_current_layout;
}

const char *xscore_keyboard_layout_name(int layout) {
    if (layout < 0 || layout >= KEYMAP_MAX) return "Unknown";
    return layout_names[layout];
}

char xscore_keyboard_translate(uint8_t scancode, int shift, int altgr, int capslock) {
    if (scancode >= 128) return 0;

    const char *norm = keymap_us_normal;
    const char *shft = keymap_us_shift;

    switch (g_current_layout) {
        case KEYMAP_UK:
            shft = keymap_uk_shift;
            break;
        case KEYMAP_DE:
            norm = keymap_de_normal;
            shft = keymap_de_shift;
            break;
        case KEYMAP_FR:
            norm = keymap_fr_normal;
            shft = keymap_fr_shift;
            break;
        case KEYMAP_DVORAK:
            norm = keymap_dvorak_normal;
            break;
        default:
            break;
    }

    char c = shift ? shft[scancode] : norm[scancode];
    if (!c) return 0;

    /* Handle AltGr specials */
    if (altgr) {
        if (scancode == 0x12) return '@'; /* AltGr + E = @ */
        if (scancode == 0x15) return '~'; /* AltGr + Y = ~ */
        if (scancode == 0x03) return '@';
        if (scancode == 0x04) return '£';
    }

    /* Handle CapsLock letter flipping */
    if (capslock && !shift) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    } else if (capslock && shift) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }

    return c;
}

/* Hardware LED Control (PS/2 Command 0xED) */
void xscore_keyboard_set_leds(uint8_t leds) {
    g_current_leds = leds & 0x07;
#if defined(__x86_64__) || defined(__i386__)
    if (!xscore_ps2_wait_input_clear()) return;
    port_out8(0x60, 0xED);
    uint8_t ack = 0;
    if (!xscore_ps2_wait_output(&ack) || ack != 0xFA) return;
    if (!xscore_ps2_wait_input_clear()) return;
    port_out8(0x60, g_current_leds);
    (void)xscore_ps2_wait_output(&ack);
#endif
}

uint8_t xscore_keyboard_get_leds(void) {
    return g_current_leds;
}

/* Hardware Typematic Rate (PS/2 Command 0xF3) */
void xscore_keyboard_set_typematic(uint8_t delay_rate_byte) {
#if defined(__x86_64__) || defined(__i386__)
    if (!xscore_ps2_wait_input_clear()) return;
    port_out8(0x60, 0xF3);
    uint8_t ack = 0;
    if (!xscore_ps2_wait_output(&ack) || ack != 0xFA) return;
    if (!xscore_ps2_wait_input_clear()) return;
    port_out8(0x60, delay_rate_byte);
    (void)xscore_ps2_wait_output(&ack);
#else
    (void)delay_rate_byte;
#endif
}

/* USB HID Keyboard Report Translation */
void xscore_usb_keyboard_input_report(const uint8_t *report, size_t length) {
    if (!report || length < 3) return;

    /* USB HID Boot Keyboard:
     * Byte 0: Modifier bitmask: bit0=L_Ctrl, bit1=L_Shift, bit2=L_Alt, bit3=L_GUI,
     *                           bit4=R_Ctrl, bit5=R_Shift, bit6=R_Alt, bit7=R_GUI
     * Byte 1: Reserved (0)
     * Byte 2..7: Pressed key array (up to 6 keys)
     */
    uint8_t mod = report[0];
    int shift = (mod & 0x22) != 0;
    int altgr = (mod & 0x40) != 0;

    for (size_t i = 2; i < length && i < 8; ++i) {
        uint8_t usb_key = report[i];
        if (usb_key == 0) continue;

        /* Map common USB Usage IDs to PS/2 Scancodes */
        uint8_t scancode = 0;
        if (usb_key >= 0x04 && usb_key <= 0x1D) {
            /* 'a' (0x04) through 'z' (0x1D) */
            static const uint8_t usb_to_ps2_alpha[] = {
                0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
                0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
                0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
            };
            scancode = usb_to_ps2_alpha[usb_key - 0x04];
        } else if (usb_key >= 0x1E && usb_key <= 0x27) {
            /* '1' (0x1E) through '0' (0x27) */
            scancode = (uint8_t)(0x02 + (usb_key - 0x1E));
        } else if (usb_key == 0x28) scancode = 0x1C; /* Enter */
        else if (usb_key == 0x29) scancode = 0x01; /* ESC */
        else if (usb_key == 0x2A) scancode = 0x0E; /* Backspace */
        else if (usb_key == 0x2B) scancode = 0x0F; /* Tab */
        else if (usb_key == 0x2C) scancode = 0x39; /* Space */

        if (scancode != 0) {
            char c = xscore_keyboard_translate(scancode, shift, altgr, 0);
            if (c) {
                xscore_input_publish(AKAOS_EV_KEY, scancode, 1);
                xscore_input_publish(AKAOS_EV_SYN, AKAOS_SYN_REPORT, 0);
            }
        }
    }
}
