#ifndef AKAOS_IOCTL_H
#define AKAOS_IOCTL_H

#include <stdint.h>

/* Return the currently readable byte count for pipe descriptors. */
#define AKAOS_FIONREAD UINT32_C(0x541B)

#endif
