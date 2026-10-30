#ifndef AKAOS_LINUX_INPUT_EVENT_CODES_H
#define AKAOS_LINUX_INPUT_EVENT_CODES_H

/*
 * Linux-compatible button codes consumed by Wayland clients. These are the
 * protocol-level code values; this header does not implement an evdev device
 * or kernel input-event interface.
 */
#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112
#define BTN_SIDE     0x113
#define BTN_EXTRA    0x114
#define BTN_FORWARD  0x115
#define BTN_BACK     0x116
#define BTN_TASK     0x117
#define BTN_STYLUS3  0x149
#define BTN_TOUCH    0x14a
#define BTN_STYLUS   0x14b
#define BTN_STYLUS2  0x14c

#endif
