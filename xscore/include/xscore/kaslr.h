#ifndef XSCORE_KASLR_H
#define XSCORE_KASLR_H

#include <stdint.h>
#include <stddef.h>
#include <akaos/boot_info.h>

/* Kernel Address Space Layout Randomization (KASLR) Subsystem */
void xscore_kaslr_initialize(const struct akaos_boot_info *boot_info);
uint64_t xscore_kaslr_get_slide(void);
uint64_t xscore_kaslr_get_physical_slide(void);
int xscore_kaslr_is_active(void);

#endif /* XSCORE_KASLR_H */
