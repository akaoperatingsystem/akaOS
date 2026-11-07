#ifndef _SHIM_LINUX_KERNEL_SCHED_SCHED_H
#define _SHIM_LINUX_KERNEL_SCHED_SCHED_H

#include <shim/linux/types.h>

// Minimal Linux scheduler shim
struct task_struct;

void schedule(void);

#endif /* _SHIM_LINUX_KERNEL_SCHED_SCHED_H */
