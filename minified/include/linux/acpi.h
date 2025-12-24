#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/property.h>
#include <linux/uuid.h>
#ifndef _LINUX
#define _LINUX
#endif
typedef void *acpi_handle;
#define acpi_disabled 1
struct fwnode_handle;
static inline void acpi_early_init(void) { }
static inline void acpi_subsystem_init(void) { }
static inline int early_acpi_boot_init(void) { return 0; }
static inline int acpi_boot_init(void) { return 0; }
static inline void acpi_boot_table_init(void) {}
static inline int acpi_mps_check(void) { return 0; }
static inline bool acpi_driver_match_device(struct device *dev, const struct device_driver *drv) { return false; }
static inline void acpi_table_upgrade(void) { }
static inline void acpi_device_notify_remove(struct device *dev) { }
#endif
