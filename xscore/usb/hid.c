
#include <xscore/usb.h>
#include <xscore/mouse.h>
#include <xscore/input.h>
#include <akaos/input.h>
#include <stddef.h>
#include <stdint.h>

/* Poll USB device for HID events */
static int hid_mouse_read_event(int *dx, int *dy, int *buttons) {
    uint8_t buffer[8];
    /* Use the existing usb interrupt transfer helper */
    /* slot_id 2 is our pointer device per usb.c */
    int bytes_read = xscore_usb_interrupt_transfer(2, 1, buffer, sizeof(buffer), 1);
    if (bytes_read < 3) return 0;

    *dx = (int8_t)buffer[1];
    *dy = -(int8_t)buffer[2];
    *buttons = buffer[0] & 7;
    
    return 1;
}

static struct xscore_mouse_driver hid_driver = {
    .type = MOUSE_TYPE_USB_HID,
    .initialize = NULL, 
    .read_event = hid_mouse_read_event
};

void xscore_usb_hid_register(void) {
    xscore_mouse_register_driver(&hid_driver);
}
