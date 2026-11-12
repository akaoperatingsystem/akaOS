
#include <xscore/mouse.h>
#include <xscore/input.h>
#include <stddef.h>

static struct xscore_mouse_driver *drivers[8];
static int driver_count = 0;

void xscore_mouse_register_driver(struct xscore_mouse_driver *driver) {
    if (driver_count < 8) {
        drivers[driver_count++] = driver;
        if (driver->initialize) {
            driver->initialize();
        }
    }
}
