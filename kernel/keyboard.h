/* ============================================================
 * akaOS — PS/2 Keyboard Driver Header
 * ============================================================ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Special key codes (not ASCII — above 0x7F) */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_DELETE  0x86
#define KEY_PGUP    0x87
#define KEY_PGDN    0x88

/* Initialize the keyboard driver (installs IRQ1 handler) */
void keyboard_init(void);

/* Get a character from the keyboard buffer (blocking) */
char keyboard_getchar(void);

/* Check if a character is available in the buffer */
int keyboard_has_char(void);

#endif /* KEYBOARD_H */
