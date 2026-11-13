#ifndef XSCORE_MOUSE_H
#define XSCORE_MOUSE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MOUSE_TYPE_PS2,
    MOUSE_TYPE_USB_HID,
    MOUSE_TYPE_SERIAL
} xscore_mouse_type_t;

struct xscore_mouse_driver {
    xscore_mouse_type_t type;
    int (*initialize)(void);
    int (*read_event)(int *dx, int *dy, int *buttons);
    int (*set_acceleration)(int profile);
};

/* Core Subsystem Registration */
void xscore_mouse_register_driver(struct xscore_mouse_driver *driver);
void xscore_mouse_init_all(void);

/* Hardware Ingress Hooks */
void ps2_mouse_register(void);
void ps2_mouse_input_byte(uint8_t value);

void xscore_serial_mouse_register(void);
void xscore_serial_mouse_input_byte(uint8_t byte);

void xscore_usb_mouse_register(void);
void xscore_usb_mouse_input_report(const uint8_t *report, size_t length);

#endif /* XSCORE_MOUSE_H */
