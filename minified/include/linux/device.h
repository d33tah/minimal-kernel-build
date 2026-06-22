
#ifndef _DEVICE_H_
#define _DEVICE_H_

/* --- 2025-12-07 23:58 --- Inlined from linux/dev_printk.h */
#include <linux/compiler.h>

#ifndef dev_fmt
#define dev_fmt(fmt) fmt
#endif

/* dev_printk stubs - only defining those actually used */
#define dev_crit(dev, fmt, ...) do { } while (0)
#define dev_err(dev, fmt, ...) do { } while (0)
#define dev_warn(dev, fmt, ...) do { } while (0)
#define dev_dbg(dev, fmt, ...) do { } while (0)
/* end dev_printk.h */

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/device/bus.h>
#include <linux/device/class.h>
#include <linux/device/driver.h>

struct device;
struct device_private;
struct driver_private;
struct module;
struct class;
struct subsys_private;
struct bus_dma_region;

/* struct subsys_interface removed - never instantiated (no
   subsys_interface_register on this build). */

struct device_type {
	/* name/groups/pm removed - never read (only ->release dispatched) */
	void (*release)(struct device *dev);
};

/* struct device_attribute + DEVICE_ATTR macro removed - 0 users tree-wide. */

/* Removed: device_remove_file (0-caller no-op). */

/* devres alloc/add/free + devm_kstrdup/devm_kasprintf removed - never called */


struct device {
	struct kobject kobj;
	struct device		*parent;

	struct device_private	*p;

	const char		*init_name;  
	const struct device_type *type;

	void	(*release)(struct device *dev);
};

static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}




static inline const char *dev_name(const struct device *dev)
{
	 
	if (dev->init_name)
		return dev->init_name;

	return kobject_name(&dev->kobj);
}


__printf(2, 3) int dev_set_name(struct device *dev, const char *name, ...);

int __must_check device_register(struct device *dev);
void device_initialize(struct device *dev);
int __must_check device_add(struct device *dev);


/* lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs,
   device_offline, device_online, set_primary_fwnode, set_secondary_fwnode,
   device_set_of_node_from_dev, device_set_node, __root_device_register,
   root_device_unregister removed - unused */

__printf(5, 6) struct device *
device_create(struct class *cls, struct device *parent, dev_t devt,
	      void *drvdata, const char *fmt, ...);
/* device_create_with_groups removed - sole caller switched to device_create */



struct device *get_device(struct device *dev);
void put_device(struct device *dev);


/* device_link_add, device_link_del, device_link_remove,
   device_links_supplier_sync_state_pause, device_links_supplier_sync_state_resume removed - unused */

#endif  
