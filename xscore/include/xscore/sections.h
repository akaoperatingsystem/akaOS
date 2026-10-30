#ifndef XSCORE_SECTIONS_H
#define XSCORE_SECTIONS_H

#include <stdint.h>

/*
 * Linker-provided section boundaries.
 * These symbols have no size; take their addresses with &.
 */
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __kernel_end[];

/*
 * Apply per-page permissions to kernel sections.
 *   .text    → Read + Execute (not writable)
 *   .rodata  → Read only     (not writable, not executable)
 *   .data    → Read + Write  (not executable)
 *   .bss     → Read + Write  (not executable)
 *
 * Requires NXE bit in EFER MSR to be enabled first.
 * Returns 1 on success, 0 on failure.
 */
int xscore_apply_section_permissions(void);

#endif
