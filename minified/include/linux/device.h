
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/compiler.h>

/* end dev_printk.h */

#include <linux/ioport.h>
#include <linux/kobject.h>
/* klist.h inlined */
struct klist_node;
struct klist {
	spinlock_t		k_lock;
	struct list_head	k_list;
	void			(*get)(struct klist_node *);
	void			(*put)(struct klist_node *);
} __attribute__ ((aligned (sizeof(void *))));
struct klist_node {
	void			*n_klist;
	struct list_head	n_node;
	struct kref		n_ref;
};
extern int klist_node_attached(struct klist_node *n);
struct klist_iter {
	struct klist		*i_klist;
	struct klist_node	*i_cur;
};
extern void klist_iter_init_node(struct klist *k, struct klist_iter *i,
				 struct klist_node *n);
extern void klist_iter_exit(struct klist_iter *i);
extern struct klist_node *klist_next(struct klist_iter *i);
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/mutex.h>
/* pm.h inlined */
struct dev_pm_ops {
};
struct dev_pm_info {
	u32			driver_flags;
};
struct dev_pm_domain {
};
#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
struct device;
struct device_driver;

struct bus_type {
	const char		*name;
	const char		*dev_name;
	int (*match)(struct device *dev, struct device_driver *drv);
	int (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);
	int (*dma_configure)(struct device *dev);
	void (*dma_cleanup)(struct device *dev);
	struct subsys_private *p;
	struct lock_class_key lock_key;
	bool need_parent_lock;
};

int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *));
/* End device/bus.h */
struct class {
	const char		*name;
	struct module		*owner;
	void (*dev_release)(struct device *dev);
	struct subsys_private *p;
};
#include <linux/module.h>
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

struct device;
struct device_private;
struct device_driver;
struct class;

struct device_type {
	const char *name;
	void (*release)(struct device *dev);
};

struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

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

	struct fwnode_handle	*fwnode;  

	dev_t			devt;	 
	u32			id;	 

	spinlock_t		devres_lock;
	struct list_head	devres_head;

	struct class		*class;

	void	(*release)(struct device *dev);

	bool			can_match:1;
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

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

static inline void device_lock(struct device *dev)
{
	mutex_lock(&dev->mutex);
}

static inline void device_unlock(struct device *dev)
{
	mutex_unlock(&dev->mutex);
}

/* lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs,
   device_offline, device_online, set_primary_fwnode, set_secondary_fwnode,
   device_set_of_node_from_dev, device_set_node, __root_device_register,
   root_device_unregister removed - unused */

void device_initial_probe(struct device *dev);

struct device *get_device(struct device *dev);
void put_device(struct device *dev);

/* device_link_add, device_link_del, device_link_remove, device_shutdown,
   device_links_supplier_sync_state_pause, device_links_supplier_sync_state_resume removed - unused */

#endif  
