#ifndef _SHIM_LINUX_ATA_AHCI_H
#define _SHIM_LINUX_ATA_AHCI_H

#include <shim/linux/types.h>

#define AHCI_PORT_PRIV_DMA_SZ 4096
#define AHCI_CMD_SLOT_MASK    0x1f

/* Generic AHCI Host Adapter Control Registers */
#define HOST_CAP        0x00
#define HOST_CTL        0x04
#define HOST_IRQ_STAT   0x08
#define HOST_PORTS_IMPL 0x0c
#define HOST_VERSION    0x10

/* Port register offsets */
#define PORT_CLB        0x00
#define PORT_CLBU       0x04
#define PORT_FB         0x08
#define PORT_FBU        0x0c
#define PORT_IRQ_STAT   0x10
#define PORT_IRQ_MASK   0x14
#define PORT_CMD        0x18
#define PORT_TFDATA     0x20
#define PORT_SIG        0x24
#define PORT_SCR_STAT   0x28
#define PORT_SCR_CTL    0x2c
#define PORT_SCR_ERR    0x30
#define PORT_SCR_ACT    0x34
#define PORT_CMD_ISSUE  0x38

#endif /* _SHIM_LINUX_ATA_AHCI_H */
