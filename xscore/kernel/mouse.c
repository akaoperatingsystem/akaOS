/*
 * Universal Mouse & Pointing Device Subsystem Core for XSCore
 * Manages drivers for PS/2, USB HID, and Serial mice
 */

#include <xscore/mouse.h>
#include <xscore/input.h>
#include <xscore/serial.h>
#include <stddef.h>

#define MAX_MOUSE_DRIVERS 8

static struct xscore_mouse_driver *drivers[MAX_MOUSE_DRIVERS];
static int driver_count = 0;

void xscore_mouse_register_driver(struct xscore_mouse_driver *driver) {
    if (!driver) return;
    if (driver_count < MAX_MOUSE_DRIVERS) {
        drivers[driver_count++] = driver;
        if (driver->initialize) {
            driver->initialize();
        }
    }
}

void xscore_mouse_init_all(void) {
    xscore_serial_write("[INPUT] Initializing Universal Pointing Device Subsystem\n");
    ps2_mouse_register();
    xscore_usb_mouse_register();
    xscore_serial_mouse_register();
    xscore_serial_write("[INPUT] Mouse Subsystem online (PS/2, USB HID, Serial UART)\n");
}
