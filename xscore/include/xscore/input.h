#ifndef XSCORE_INPUT_H
#define XSCORE_INPUT_H

#include <akaos/input.h>
#include <stddef.h>
#include <stdint.h>

void xscore_input_initialize(void);
void xscore_input_publish(uint16_t type, uint16_t code, int32_t value);
uint64_t xscore_input_open_cursor(void);
int xscore_input_ready(uint64_t cursor);
int64_t xscore_input_read(uint64_t *cursor, void *buffer, uint64_t length);
void xscore_input_client_open(void);
void xscore_input_client_close(void);
int xscore_input_is_grabbed(void);

#endif
