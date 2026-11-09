#ifndef XSCORE_USB_H
#define XSCORE_USB_H

#include <stdint.h>
#include <stddef.h>

/* USB Standard Request Codes */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SYNCH_FRAME       0x0C

/* USB Descriptor Types */
#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIGURATION    0x02
#define USB_DESC_STRING           0x03
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05
#define USB_DESC_DEVICE_QUALIFIER 0x06
#define USB_DESC_OTHER_SPEED_CONF 0x07
#define USB_DESC_INTERFACE_POWER  0x08
#define USB_DESC_OTG              0x09
#define USB_DESC_DEBUG            0x0A
#define USB_DESC_INTERFACE_ASSOC  0x0B
#define USB_DESC_BOS              0x0F
#define USB_DESC_DEVICE_CAPABILITY 0x10
#define USB_DESC_HID              0x21
#define USB_DESC_HID_REPORT       0x22
#define USB_DESC_HID_PHYSICAL     0x23
#define USB_DESC_HUB              0x29
#define USB_DESC_SUPERSPEED_HUB   0x2A
#define USB_DESC_SS_EP_COMPANION  0x30

/* USB Standard Classes */
#define USB_CLASS_PER_INTERFACE   0x00
#define USB_CLASS_AUDIO           0x01
#define USB_CLASS_COMM            0x02
#define USB_CLASS_HID             0x03
#define USB_CLASS_PHYSICAL        0x05
#define USB_CLASS_IMAGE           0x06
#define USB_CLASS_PRINTER         0x07
#define USB_CLASS_MASS_STORAGE    0x08
#define USB_CLASS_HUB             0x09
#define USB_CLASS_DATA            0x0A
#define USB_CLASS_SMART_CARD      0x0B
#define USB_CLASS_CONTENT_SEC     0x0D
#define USB_CLASS_VIDEO           0x0E
#define USB_CLASS_PERSONAL_HEALTH 0x0F
#define USB_CLASS_AUDIO_VIDEO     0x10
#define USB_CLASS_BILLBOARD       0x11
#define USB_CLASS_USB_TYPE_C      0x12
#define USB_CLASS_DIAGNOSTIC      0xDC
#define USB_CLASS_WIRELESS        0xE0
#define USB_CLASS_MISC            0xEF
#define USB_CLASS_VENDOR_SPEC     0xFF

/* USB Endpoint Types */
#define USB_EP_CONTROL            0x00
#define USB_EP_ISOCHRONOUS        0x01
#define USB_EP_BULK               0x02
#define USB_EP_INTERRUPT          0x03

/* USB Device Speeds */
#define USB_SPEED_UNKNOWN         0
#define USB_SPEED_LOW             1 /* 1.5 Mbps */
#define USB_SPEED_FULL            2 /* 12 Mbps */
#define USB_SPEED_HIGH            3 /* 480 Mbps */
#define USB_SPEED_SUPER           4 /* 5 Gbps */
#define USB_SPEED_SUPER_PLUS      5 /* 10/20 Gbps */

#pragma pack(push, 1)

struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
};

struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
};

struct usb_interface_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
};

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
};

struct usb_hid_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wDescriptorLength;
};

#pragma pack(pop)

#define XSCORE_USB_MAX_DEVICES 32U

struct xscore_usb_device {
    uint8_t  slot_id;
    uint8_t  port_num;
    uint8_t  speed;
    uint8_t  address;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    char     product_name[48];
    char     manufacturer[48];
    char     serial_number[32];
    uint8_t  configured;
    uint8_t  active;
    /* Endpoint allocations */
    uint8_t  ep_in_addr;
    uint8_t  ep_out_addr;
    uint16_t ep_in_max_packet;
    uint16_t ep_out_max_packet;
    uint8_t  ep_in_type;
    uint8_t  ep_out_type;
};

/* Core Subsystem Lifecycle */
int xscore_usb_initialize(void);
uint32_t xscore_usb_device_count(void);
const struct xscore_usb_device *xscore_usb_get_device(uint32_t index);
const struct xscore_usb_device *xscore_usb_find_class(uint8_t device_class);

/* Transfers */
int xscore_usb_control_transfer(uint8_t slot_id, const struct usb_setup_packet *setup, void *data, uint16_t length);
int xscore_usb_bulk_transfer(uint8_t slot_id, uint8_t endpoint, void *data, uint32_t length, int is_in);
int xscore_usb_interrupt_transfer(uint8_t slot_id, uint8_t endpoint, void *data, uint32_t length, int is_in);

/* Diagnostics */
size_t xscore_usb_format_proc_devices(char *buf, size_t max_size);
const char *xscore_usb_class_name(uint8_t device_class);
const char *xscore_usb_speed_name(uint8_t speed);

#endif /* XSCORE_USB_H */
