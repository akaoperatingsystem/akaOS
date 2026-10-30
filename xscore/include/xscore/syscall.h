#ifndef XSCORE_SYSCALL_H
#define XSCORE_SYSCALL_H

#include <akaos/syscall.h>
#include <stdint.h>

#if defined(__i386__) || defined(__arm__) || (defined(__riscv) && __riscv_xlen == 32)
#define XSCORE_SYSCALL_EXIT_TO_KERNEL UINT64_C(0x7FFFFFFF)
#else
#define XSCORE_SYSCALL_EXIT_TO_KERNEL UINT64_C(0x8000000000000000)
#endif

uint64_t xscore_syscall_dispatch(uint64_t number, uint64_t argument0,
                                 uint64_t argument1, uint64_t argument2);
uint64_t xscore_syscall_last_exit_status(void);
void xscore_syscall_prepare_process(void);
void xscore_syscall_futex_timer_tick(void);

#endif
