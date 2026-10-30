#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

#include <akaos/syscall.h>

#define RB_AUTOBOOT AKAOS_RB_REBOOT
#define RB_POWER_OFF AKAOS_RB_POWEROFF
#define RB_HALT_SYSTEM AKAOS_RB_HALT

int reboot(int cmd);

#endif
