#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/uuid.h>
#ifndef _LINUX
#define _LINUX
#endif
#define acpi_disabled 1
struct fwnode_handle;
/* acpi_early_init, acpi_subsystem_init, acpi_table_upgrade,
   acpi_device_notify_remove removed - call sites removed */
#endif
