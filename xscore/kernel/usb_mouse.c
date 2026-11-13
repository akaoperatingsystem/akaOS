/*
 * USB HID Pointing Device Driver for XSCore
 * Supports USB HID Boot Protocol and Report Protocol for mice, trackballs, and gaming pointers
 */

#include <xscore/mouse.h>
#include <xscore/input.h>
#include <akaos/input.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t s_usb_mouse_buttons = 0;

void xscore_usb_mouse_input_report(const uint8_t *report, size_t length) {
    if (!report || length < 3) return;

    /* Standard USB HID Boot Protocol Mouse Report:
     * Byte 0: Button bitmask: Bit 0=Left, Bit 1=Right, Bit 2=Middle, Bit 3=Side, Bit 4=Extra
     * Byte 1: X displacement (signed 8-bit)
     * Byte 2: Y displacement (signed 8-bit)
     * Byte 3 (Optional): Wheel displacement (signed 8-bit)
     * Byte 4 (Optional): Horizontal wheel displacement (signed 8-bit)
     */
    uint8_t buttons = report[0];
    int32_t dx = (int32_t)(int8_t)report[1];
    int32_t dy = (int32_t)(int8_t)report[2];

    if (dx != 0) {
        xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_X, dx);
    }
    if (dy != 0) {
        xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_Y, dy);
    }

    /* Optional vertical and horizontal scroll wheel */
    if (length >= 4) {
        int32_t wheel = (int32_t)(int8_t)report[3];
        if (wheel != 0) {
            xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_WHEEL, wheel);
        }
    }
    if (length >= 5) {
        int32_t hwheel = (int32_t)(int8_t)report[4];
        if (hwheel != 0) {
            xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_HWHEEL, hwheel);
        }
    }

    /* Button transitions */
    if ((buttons ^ s_usb_mouse_buttons) & 0x01) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_LEFT, (buttons & 0x01) != 0);
    }
    if ((buttons ^ s_usb_mouse_buttons) & 0x02) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_RIGHT, (buttons & 0x02) != 0);
    }
    if ((buttons ^ s_usb_mouse_buttons) & 0x04) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_MIDDLE, (buttons & 0x04) != 0);
    }
    if ((buttons ^ s_usb_mouse_buttons) & 0x08) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_SIDE, (buttons & 0x08) != 0);
    }
    if ((buttons ^ s_usb_mouse_buttons) & 0x10) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_EXTRA, (buttons & 0x10) != 0);
    }

    s_usb_mouse_buttons = buttons;
    xscore_input_publish(AKAOS_EV_SYN, AKAOS_SYN_REPORT, 0);
}

static int usb_mouse_initialize(void) {
    s_usb_mouse_buttons = 0;
    return 1;
}

static int usb_mouse_read_event(int *dx, int *dy, int *buttons) {
    (void)dx; (void)dy; (void)buttons;
    return 0;
}

static struct xscore_mouse_driver s_usb_mouse_driver = {
    .type = MOUSE_TYPE_USB_HID,
    .initialize = usb_mouse_initialize,
    .read_event = usb_mouse_read_event
};

void xscore_usb_mouse_register(void) {
    xscore_mouse_register_driver(&s_usb_mouse_driver);
}
