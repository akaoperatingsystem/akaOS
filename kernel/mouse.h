/* ============================================================
 * akaOS — PS/2 Mouse Driver Header
 * ============================================================ */
#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

void mouse_init(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_buttons(void); /* bit0=left, bit1=right, bit2=middle */
void mouse_set_bounds(int w, int h);

#endif
