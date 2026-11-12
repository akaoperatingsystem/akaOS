
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

void xscore_mouse_register_driver(struct xscore_mouse_driver *driver);

void ps2_mouse_input_byte(uint8_t value);
#endif /* XSCORE_MOUSE_H */
