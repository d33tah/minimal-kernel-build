
#ifndef _DEVICE_H_
#define _DEVICE_H_

/* --- 2025-12-07 23:58 --- Inlined from linux/dev_printk.h */
#include <linux/compiler.h>

/* dev_printk stub - only dev_dbg has a caller (tty_io.c) */
#define dev_dbg(dev, fmt, ...) do { } while (0)
/* end dev_printk.h */

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/device/class.h>
#include <linux/device/driver.h>

struct device_private;
struct class;

/* struct subsys_interface removed - never instantiated (no
   subsys_interface_register on this build). */


/* struct device_attribute + DEVICE_ATTR macro removed - 0 users tree-wide. */

/* Removed: device_remove_file (0-caller no-op). */

/* devres alloc/add/free + devm_kstrdup/devm_kasprintf removed - never called */


struct device { struct kobject kobj; struct device		*parent; struct device_private	*p; void	(*release)(struct device *dev); };

static inline struct device *kobj_to_dev(struct kobject *kobj) {
	return container_of(kobj, struct device, kobj); }




static inline const char *dev_name(const struct device *dev) {
	/* init_name removed - never set non-NULL by any device, so this
	 * always fell through to the kobject name */
	return kobject_name(&dev->kobj); }


__printf(2, 3) int dev_set_name(struct device *dev, const char *name, ...);

int __must_check device_register(struct device *dev);
void device_initialize(struct device *dev);
int __must_check device_add(struct device *dev);


/* lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs,
   device_offline, device_online, set_primary_fwnode, set_secondary_fwnode,
   device_set_of_node_from_dev, device_set_node, __root_device_register,
   root_device_unregister removed - unused */

__printf(5, 6) struct device * device_create(struct class *cls, struct device *parent, dev_t devt, void *drvdata, const char *fmt, ...);
/* device_create_with_groups removed - sole caller switched to device_create */



struct device *get_device(struct device *dev);
void put_device(struct device *dev);


/* device_link_add, device_link_del, device_link_remove,
   device_links_supplier_sync_state_pause, device_links_supplier_sync_state_resume removed - unused */

#endif  
