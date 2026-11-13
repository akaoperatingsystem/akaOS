#ifndef XSCORE_KEYBOARD_H
#define XSCORE_KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

enum xscore_keyboard_layout {
    KEYMAP_US = 0,
    KEYMAP_UK,
    KEYMAP_DE,
    KEYMAP_FR,
    KEYMAP_ES,
    KEYMAP_NORDIC,
    KEYMAP_DVORAK,
    KEYMAP_MAX
};

/* LED flags */
#define KEYBOARD_LED_SCROLL_LOCK 0x01
#define KEYBOARD_LED_NUM_LOCK    0x02
#define KEYBOARD_LED_CAPS_LOCK   0x04

/* Hardware Typematic Delays */
#define TYPEMATIC_DELAY_250MS  0x00
#define TYPEMATIC_DELAY_500MS  0x20
#define TYPEMATIC_DELAY_750MS  0x40
#define TYPEMATIC_DELAY_1000MS 0x60

/* Typematic Rates (0 = 30Hz, 0x1F = 2Hz) */
#define TYPEMATIC_RATE_30HZ    0x00
#define TYPEMATIC_RATE_20HZ    0x04
#define TYPEMATIC_RATE_15HZ    0x08
#define TYPEMATIC_RATE_10HZ    0x0C
#define TYPEMATIC_RATE_2HZ     0x1F

/* Core Keyboard Layout Management */
int xscore_keyboard_set_layout(int layout);
int xscore_keyboard_get_layout(void);
const char *xscore_keyboard_layout_name(int layout);

/* Translation Pipeline */
char xscore_keyboard_translate(uint8_t scancode, int shift, int altgr, int capslock);

/* Hardware Features */
void xscore_keyboard_set_leds(uint8_t leds);
uint8_t xscore_keyboard_get_leds(void);
void xscore_keyboard_set_typematic(uint8_t delay_rate_byte);

/* USB HID Keyboard Ingress */
void xscore_usb_keyboard_input_report(const uint8_t *report, size_t length);

#endif /* XSCORE_KEYBOARD_H */
