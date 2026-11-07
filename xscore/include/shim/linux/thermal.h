#ifndef _SHIM_LINUX_THERMAL_H
#define _SHIM_LINUX_THERMAL_H

#include <shim/linux/types.h>

struct thermal_zone_device;

int thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp);

#endif /* _SHIM_LINUX_THERMAL_H */
