#ifndef AKAOS_IOCTL_H
#define AKAOS_IOCTL_H

#include <stdint.h>

/* Return the currently readable byte count for pipe descriptors. */
#define AKAOS_FIONREAD UINT32_C(0x541B)


/* NVMe Storage Device ioctls */
#define AKAOS_NVME_GET_SMART_LOG UINT32_C(0x4E01)


/* Block device ioctls */
#define AKAOS_BLKDISCARD UINT32_C(0x1277)


/* Disk Power Management ioctl */
#define AKAOS_DISK_POWER_MODE UINT32_C(0x4E03)

#endif

/* dmesg ioctls */
#define AKAOS_DMESG_CLEAR UINT32_C(0x5601)
#define AKAOS_DMESG_SETLEVEL UINT32_C(0x5602)

/* Command Subsystem IOCTLs */
#define AKAOS_EJECT            UINT32_C(0x541C)
#define AKAOS_SIOCGIFSTATS     UINT32_C(0x8901)
#define AKAOS_BLKFORMAT        UINT32_C(0x1278)
