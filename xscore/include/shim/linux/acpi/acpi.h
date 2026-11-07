#ifndef _SHIM_LINUX_ACPI_H
#define _SHIM_LINUX_ACPI_H

#include <shim/linux/types.h>

struct acpi_device;
typedef u32 acpi_status;

#define AE_OK 0x0000
#define AE_ERROR 0x0001

acpi_status acpi_enable(void);
acpi_status acpi_disable(void);

#endif /* _SHIM_LINUX_ACPI_H */
