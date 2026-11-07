#ifndef _SHIM_LINUX_BLOCK_LOOP_H
#define _SHIM_LINUX_BLOCK_LOOP_H

#include <shim/linux/types.h>

struct loop_device;

int loop_register_device(struct loop_device *dev);
void loop_unregister_device(struct loop_device *dev);

#endif /* _SHIM_LINUX_BLOCK_LOOP_H */
