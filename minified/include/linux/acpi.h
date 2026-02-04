#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H
/* linux/errno.h, linux/list.h removed - unused */
#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/device.h>
/* uuid.h removed - guid_t not used */
#ifndef _LINUX
#define _LINUX
#endif
/* acpi_disabled, struct fwnode_handle removed - never used in this header */
/* acpi_early_init, acpi_subsystem_init, acpi_table_upgrade,
   acpi_device_notify_remove removed - call sites removed */
#endif
