
#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/errno.h>
#include <linux/ioport.h>	 
#include <linux/irqdomain.h>
#include <linux/resource_ext.h>
#include <linux/device.h>
#include <linux/property.h>
#include <linux/uuid.h>

#ifndef _LINUX
#define _LINUX
#endif

typedef void *acpi_handle;
typedef u32 acpi_status;
typedef u32 acpi_object_type;
union acpi_object;

#define acpi_disabled 1

#define ACPI_COMPANION(dev)		(NULL)
#define ACPI_COMPANION_SET(dev, adev)	do { } while (0)
#define ACPI_HANDLE(dev)		(NULL)
#define ACPI_HANDLE_FWNODE(fwnode)	(NULL)
#define ACPI_DEVICE_CLASS(_cls, _msk)	.cls = (0), .cls_msk = (0),



struct fwnode_handle;

struct acpi_device;

static inline bool
acpi_dev_hid_uid_match(struct acpi_device *adev, const char *hid2, const char *uid2)
{
	return false;
}

static inline struct acpi_device *
acpi_dev_get_first_match_dev(const char *hid, const char *uid, s64 hrv)
{
	return NULL;
}

static inline struct acpi_device *to_acpi_device_node(const struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline struct acpi_data_node *to_acpi_data_node(const struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline struct fwnode_handle *acpi_fwnode_handle(struct acpi_device *adev)
{
	return NULL;
}

static inline bool has_acpi_companion(struct device *dev)
{
	return false;
}

static inline const char *acpi_dev_name(struct acpi_device *adev)
{
	return NULL;
}

static inline struct device *acpi_get_first_physical_node(struct acpi_device *adev)
{
	return NULL;
}

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

struct acpi_table_header;
struct acpi_device_id;

static inline const struct acpi_device_id *acpi_match_device(
	const struct acpi_device_id *ids, const struct device *dev)
{
	return NULL;
}

static inline const void *acpi_device_get_match_data(const struct device *dev)
{
	return NULL;
}

static inline bool acpi_driver_match_device(struct device *dev,
					    const struct device_driver *drv)
{
	return false;
}

/* Unused ACPI functions removed - acpi_evaluate_dsm through acpi_device_notify */
#define ACPI_PTR(_ptr)	(NULL)
#define acpi_handle_printk(level, handle, fmt, ...) do { } while (0)
#define acpi_handle_err(handle, fmt, ...) do { } while (0)
#define acpi_handle_warn(handle, fmt, ...) do { } while (0)
#define acpi_handle_info(handle, fmt, ...) do { } while (0)
#define acpi_handle_debug(handle, fmt, ...) do { } while (0)
static inline void acpi_table_upgrade(void) { }
static inline void acpi_device_notify(struct device *dev) { }
static inline void acpi_device_notify_remove(struct device *dev) { }

#endif	 
