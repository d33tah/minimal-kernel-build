
#ifndef _DEVICE_BUS_H_
#define _DEVICE_BUS_H_

#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/pm.h>

struct device_driver;
struct fwnode_handle;

struct bus_type {
	const char		*name;
	const char		*dev_name;
	struct device		*dev_root;
	/* bus_groups, dev_groups, drv_groups removed - never read (sysfs stubbed) */

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	void (*sync_state)(struct device *dev);
	void (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*online)(struct device *dev);
	int (*offline)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);

	int (*num_vf)(struct device *dev);

	int (*dma_configure)(struct device *dev);
	void (*dma_cleanup)(struct device *dev);

	const struct dev_pm_ops *pm;

	const struct iommu_ops *iommu_ops;

	struct subsys_private *p;
	struct lock_class_key lock_key;

	bool need_parent_lock;
};

extern int __must_check bus_register(struct bus_type *bus);
/* bus_unregister removed - never called */

struct bus_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct bus_type *bus, char *buf);
	ssize_t (*store)(struct bus_type *bus, const char *buf, size_t count);
};

/* BUS_ATTR_RW, BUS_ATTR_WO, device_match_name, device_match_of_node, device_match_fwnode,
   device_match_acpi_dev, device_match_acpi_handle, device_match_any removed - unused */
int device_match_devt(struct device *dev, const void *pdevt);


int bus_for_each_dev(struct bus_type *bus, struct device *start, void *data,
		     int (*fn)(struct device *dev, void *data));
struct device *bus_find_device(struct bus_type *bus, struct device *start,
			       const void *data,
			       int (*match)(struct device *dev, const void *data));
/* bus_find_device_by_name, bus_find_device_by_of_node, bus_find_device_by_fwnode,
 * bus_find_device_by_devt, bus_find_next_device, bus_find_device_by_acpi_dev,
 * subsys_find_device_by_id removed - unused */

int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *));

/* BUS_NOTIFY_* macros removed - unused */

#endif
