/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acpi.h - ACPI Interface
 *
 * Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/errno.h>
#include <linux/ioport.h>	/* for struct resource */
#include <linux/irqdomain.h>
#include <linux/resource_ext.h>
#include <linux/device.h>
#include <linux/property.h>
#include <linux/uuid.h>

#ifndef _LINUX
#define _LINUX
#endif
// #include <acpi/acpi.h>  // Removed - not needed since ACPI is disabled

// Define minimal ACPI types needed for stub functions
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

static inline union acpi_object *acpi_evaluate_dsm(acpi_handle handle,
						   const guid_t *guid,
						   u64 rev, u64 func,
						   union acpi_object *argv4)
{
	return NULL;
}

static inline int acpi_device_uevent_modalias(struct device *dev,
				struct kobj_uevent_env *env)
{
	return -ENODEV;
}

static inline int acpi_device_modalias(struct device *dev,
				char *buf, int size)
{
	return -ENODEV;
}

static inline struct platform_device *
acpi_create_platform_device(struct acpi_device *adev,
			    const struct property_entry *properties)
{
	return NULL;
}

static inline enum dev_dma_attr acpi_get_dma_attr(struct acpi_device *adev)
{
	return DEV_DMA_NOT_SUPPORTED;
}

static inline int acpi_dma_configure(struct device *dev,
				     enum dev_dma_attr attr)
{
	return 0;
}

#define ACPI_PTR(_ptr)	(NULL)

static inline struct acpi_device *acpi_resource_consumer(struct resource *res)
{
	return NULL;
}

struct acpi_osc_context;
#define acpi_os_set_prepare_sleep(func, pm1a_ctrl, pm1b_ctrl) do { } while (0)

static inline __printf(3, 4) void
acpi_handle_printk(const char *level, void *handle, const char *fmt, ...) {}
/*
 * acpi_handle_<level>: Print message with ACPI prefix and object path
 *
 * These interfaces acquire the global namespace mutex to obtain an object
 * path.  In interrupt context, it shows the object path as <n/a>.
 */
#define acpi_handle_emerg(handle, fmt, ...)				\
	acpi_handle_printk(KERN_EMERG, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_alert(handle, fmt, ...)				\
	acpi_handle_printk(KERN_ALERT, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_crit(handle, fmt, ...)				\
	acpi_handle_printk(KERN_CRIT, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_err(handle, fmt, ...)				\
	acpi_handle_printk(KERN_ERR, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_warn(handle, fmt, ...)				\
	acpi_handle_printk(KERN_WARNING, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_notice(handle, fmt, ...)				\
	acpi_handle_printk(KERN_NOTICE, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_info(handle, fmt, ...)				\
	acpi_handle_printk(KERN_INFO, handle, fmt, ##__VA_ARGS__)

#if defined(DEBUG)
#define acpi_handle_debug(handle, fmt, ...)				\
	acpi_handle_printk(KERN_DEBUG, handle, fmt, ##__VA_ARGS__)
#else
#define acpi_handle_debug(handle, fmt, ...)				\
({									\
	if (0)								\
		acpi_handle_printk(KERN_DEBUG, handle, fmt, ##__VA_ARGS__); \
	0;								\
})
#endif

static inline int acpi_dev_gpio_irq_get_by(struct acpi_device *adev,
					   const char *name, int index)
{
	return -ENXIO;
}

static inline int acpi_dev_gpio_irq_get(struct acpi_device *adev, int index)
{
	return acpi_dev_gpio_irq_get_by(adev, NULL, index);
}

/* Device properties */

static inline int
__acpi_node_get_property_reference(const struct fwnode_handle *fwnode,
				const char *name, size_t index, size_t num_args,
				struct fwnode_reference_args *args)
{
	return -ENXIO;
}

static inline int
acpi_node_get_property_reference(const struct fwnode_handle *fwnode,
				 const char *name, size_t index,
				 struct fwnode_reference_args *args)
{
	return -ENXIO;
}

static inline struct fwnode_handle *
acpi_get_next_subnode(const struct fwnode_handle *fwnode,
		      struct fwnode_handle *child)
{
	return NULL;
}

static inline struct fwnode_handle *
acpi_graph_get_next_endpoint(const struct fwnode_handle *fwnode,
			     struct fwnode_handle *prev)
{
	return ERR_PTR(-ENXIO);
}

static inline int
acpi_graph_get_remote_endpoint(const struct fwnode_handle *fwnode,
			       struct fwnode_handle **remote,
			       struct fwnode_handle **port,
			       struct fwnode_handle **endpoint)
{
	return -ENXIO;
}

#define ACPI_DECLARE_PROBE_ENTRY(table, name, table_id, subtable, valid, data, fn) \
	static const void * __acpi_table_##name[]			\
		__attribute__((unused))					\
		 = { (void *) table_id,					\
		     (void *) subtable,					\
		     (void *) valid,					\
		     (void *) fn,					\
		     (void *) data }

#define acpi_probe_device_table(t)	({ int __r = 0; __r;})

static inline void acpi_table_upgrade(void) { }

#if IS_ENABLED(CONFIG_ACPI_GENERIC_GSI)
int acpi_irq_get(acpi_handle handle, unsigned int index, struct resource *res);
#else
static inline
int acpi_irq_get(acpi_handle handle, unsigned int index, struct resource *res)
{
	return -EINVAL;
}
#endif

static inline void acpi_device_notify(struct device *dev) { }
static inline void acpi_device_notify_remove(struct device *dev) { }

#endif	/*_LINUX_ACPI_H*/
