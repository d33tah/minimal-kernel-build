
#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/device.h>
#include <linux/list.h>

/* Forward decls for resource_win, resource_entry removed - unused */
#include <linux/property.h>
#include <linux/uuid.h>

#ifndef _LINUX
#define _LINUX
#endif

typedef void *acpi_handle;

#define acpi_disabled 1



struct fwnode_handle;
/* struct acpi_device forward decl removed - unused */


static inline void acpi_early_init(void) { }
static inline void acpi_subsystem_init(void) { }

static inline int early_acpi_boot_init(void)
{
	return 0;
}
static inline int acpi_boot_init(void)
{
	return 0;
}

static inline void acpi_boot_table_init(void)
{
}

static inline int acpi_mps_check(void)
{
	return 0;
}

/* struct acpi_table_header, struct acpi_device_id forward decls removed - unused */


static inline bool acpi_driver_match_device(struct device *dev,
					    const struct device_driver *drv)
{
	return false;
}

#define acpi_handle_printk(level, handle, fmt, ...) do { } while (0)
#define acpi_handle_err(handle, fmt, ...) do { } while (0)
#define acpi_handle_warn(handle, fmt, ...) do { } while (0)
#define acpi_handle_info(handle, fmt, ...) do { } while (0)
#define acpi_handle_debug(handle, fmt, ...) do { } while (0)
static inline void acpi_table_upgrade(void) { }
static inline void acpi_device_notify_remove(struct device *dev) { }

#endif	 
