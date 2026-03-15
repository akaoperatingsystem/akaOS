/* ============================================================
 * akaOS — GUI Desktop Header
 * ============================================================ */
#ifndef GUI_H
#define GUI_H
#include <stdint.h>

/* Initialize and run the GUI desktop (does not return) */
void gui_init(void);
void gui_run(void);
void gui_pump(void);

extern volatile int doom_running;
extern void *doom_exit_jmp_buf[5];

#endif
