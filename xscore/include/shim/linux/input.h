#ifndef _SHIM_LINUX_INPUT_H
#define _SHIM_LINUX_INPUT_H

#include <shim/linux/types.h>

struct input_dev;
struct input_event;

int input_register_device(struct input_dev *dev);
void input_unregister_device(struct input_dev *dev);

#endif /* _SHIM_LINUX_INPUT_H */
