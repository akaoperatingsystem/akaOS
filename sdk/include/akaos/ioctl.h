#ifndef AKAOS_IOCTL_H
#define AKAOS_IOCTL_H

#include <stdint.h>

/* Return the currently readable byte count for pipe descriptors. */
#define AKAOS_FIONREAD UINT32_C(0x541B)


/* NVMe Storage Device ioctls */
#define AKAOS_NVME_GET_SMART_LOG UINT32_C(0x4E01)

#endif
