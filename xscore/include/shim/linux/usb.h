#ifndef _SHIM_LINUX_USB_H
#define _SHIM_LINUX_USB_H

#include <shim/linux/types.h>

struct usb_device;
struct usb_interface;

struct usb_driver {
    const char *name;
    int (*probe)(struct usb_interface *intf, const void *id);
    void (*disconnect)(struct usb_interface *intf);
};

int usb_register_driver(struct usb_driver *driver, void *owner, const char *mod_name);
void usb_deregister(struct usb_driver *driver);

#endif /* _SHIM_LINUX_USB_H */
