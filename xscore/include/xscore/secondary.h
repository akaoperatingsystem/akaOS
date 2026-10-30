#ifndef XSCORE_SECONDARY_H
#define XSCORE_SECONDARY_H

#include <stdint.h>

/*
 * Native secondary-CPU/hart bring-up for the non-x86 ports.
 *
 * The first implementation is deliberately bounded: firmware starts at most
 * four additional execution units, each runs a short scheduler heartbeat,
 * then parks.  This proves the boot and per-CPU stack contract without
 * allowing an AP to enter the single-address-space process code before its
 * per-CPU scheduler context is ready.
 */
#define XSCORE_SECONDARY_BOOT_LIMIT 4U

int xscore_secondary_initialize(void);
void xscore_secondary_cpu_entry(uint32_t cpu_index);
uint32_t xscore_secondary_online_count(void);
uint32_t xscore_secondary_heartbeat(uint32_t cpu_index);
uint32_t xscore_secondary_current_cpu_index(void);
uint32_t xscore_secondary_raw_cpu_id(void);
uint64_t xscore_secondary_stack_top(uint32_t raw_cpu_id);

#endif /* XSCORE_SECONDARY_H */
