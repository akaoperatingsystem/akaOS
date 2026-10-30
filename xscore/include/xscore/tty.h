#ifndef XSCORE_TTY_H
#define XSCORE_TTY_H

#include <stddef.h>
#include <stdint.h>
#include <akaos/termios.h>

void xscore_tty_initialize(void);
void xscore_tty_input_scancode(uint8_t scan_code);
void xscore_tty_input_char(char c);
int64_t xscore_tty_read(void *buf, uint64_t count);
int xscore_tty_has_line(void);
void xscore_tty_poll_keyboard(void);
int xscore_tty_enable_interrupts(void);
int xscore_tty_interrupts_enabled(void);
int xscore_tty_set_echo(int enabled);
void xscore_tty_get_termios(struct akaos_termios *settings);
void xscore_tty_set_termios(const struct akaos_termios *settings);
void xscore_tty_record_output(const char *buf, size_t len);

#endif
