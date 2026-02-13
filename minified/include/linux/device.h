
#ifndef _DEVICE_H_
#define _DEVICE_H_

/* --- 2025-12-07 23:58 --- Inlined from linux/dev_printk.h */
#include <linux/compiler.h>

/* PRINTK_INFO_*, struct dev_printk_info removed - never instantiated */

/* dev_printk stubs all removed - dev_crit, dev_err, dev_warn, dev_dbg, dev_err_once, dev_WARN_ONCE */
/* end dev_printk.h */

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/klist.h>
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
/* Inlined from linux/device/class.h */
struct class {
	const char		*name;
	struct module		*owner;
	void (*dev_release)(struct device *dev);
	struct subsys_private *p;
};
#include <linux/module.h>
/* Inlined from device/driver.h */
struct device_driver {
	const char		*name;
	struct bus_type		*bus;
	struct module		*owner;
	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	struct driver_private *p;
};

extern void wait_for_device_probe(void);
void driver_init(void);
/* End device/driver.h */
/* struct dev_archdata, pdev_archdata removed - unused */

/* Unused forward decls removed: driver_private, module, subsys_private, iommu_ops, device_node, fwnode_handle */
struct device;
struct device_private;
struct device_driver;
struct class;

/* struct subsys_interface removed - never instantiated after subsys_interface_register removal */

/* subsys_system_register removed - never called */

struct device_type {
	const char *name;
	/* groups, uevent, devnode, pm removed - never accessed */
	void (*release)(struct device *dev);
};

struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

/* DEVICE_ATTR removed - never used */

/* device_remove_file, devres functions, struct device_dma_parameters removed - never called */

enum dl_dev_state {
	DL_DEV_NO_DRIVER = 0,
	DL_DEV_PROBING,
};

struct dev_links_info {
	struct list_head suppliers;
	struct list_head consumers;
	struct list_head defer_sync;
	enum dl_dev_state status;
};

/* struct dev_msi_info removed - never accessed */

/* device_physical_location - not used in minimal kernel */
/* struct device_physical_location removed - never used */

struct device {
	struct kobject kobj;
	struct device		*parent;

	struct device_private	*p;

	const char		*init_name;  
	const struct device_type *type;

	struct bus_type	*bus;
	struct device_driver *driver;
	void		*driver_data;
	struct mutex		mutex;	 

	struct dev_links_info	links;
	struct dev_pm_info	power;
	struct dev_pm_domain	*pm_domain;


	/* dev_msi_info msi, dma_mask, coherent_dma_mask, bus_dma_limit, dma_range_map, dma_pools, of_node removed - never accessed */

	/* of_node removed - never accessed */
	struct fwnode_handle	*fwnode;  

	dev_t			devt;	 
	u32			id;	 

	spinlock_t		devres_lock;
	struct list_head	devres_head;

	struct class		*class;
	/* groups field removed - only written, never read (sysfs stubbed) */

	void	(*release)(struct device *dev);

	bool			can_match:1;
};

/* struct device_link forward decl removed - never used */

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


/* dev_set_name removed - no callers */

/* dev_to_node, set_dev_node removed - never called */

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}


/* dev_set_uevent_suppress removed - empty stub, all callers removed */
/* device_is_registered removed - inlined at single call site */
/* device_set_pm_not_required removed - never called */
/* dev_pm_set_driver_flags removed - inlined at single call site */

static inline void device_lock(struct device *dev)
{
	mutex_lock(&dev->mutex);
}


static inline void device_unlock(struct device *dev)
{
	mutex_unlock(&dev->mutex);
}

/* device_lock_assert removed - was empty stub */

/* device_initialize, device_add removed - no callers */


/* lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs,
   device_offline, device_online, set_primary_fwnode, set_secondary_fwnode,
   device_set_of_node_from_dev, device_set_node, __root_device_register,
   root_device_unregister removed - unused */

/* driver_attach removed - only caller was bus_add_driver */
void device_initial_probe(struct device *dev);

/* device_create, device_destroy removed - no callers */

/* device_add_groups, device_remove_groups removed - calls removed */

struct device *get_device(struct device *dev);
void put_device(struct device *dev);

/* devtmpfs_mount removed - unused */

/* device_link_add, device_link_del, device_link_remove, device_shutdown,
   device_links_supplier_sync_state_pause, device_links_supplier_sync_state_resume removed - unused */
/* dev_err_probe removed - never called */

#endif  
