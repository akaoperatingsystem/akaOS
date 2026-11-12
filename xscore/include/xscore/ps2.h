
#ifndef XSCORE_PS2_H
#define XSCORE_PS2_H
#include <stdint.h>
int xscore_ps2_wait_input_clear(void);
int xscore_ps2_wait_output(uint8_t *value);
void xscore_ps2_flush_output(void);
#endif
