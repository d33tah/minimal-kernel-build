
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
	/* dev_root, bus_groups, dev_groups, drv_groups, uevent removed - never read/called (sysfs stubbed) */

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*probe)(struct device *dev);
	/* sync_state removed - never called */
	void (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	/* online, offline, num_vf, suspend, resume, pm, iommu_ops removed - never called/accessed */

	int (*dma_configure)(struct device *dev);
	void (*dma_cleanup)(struct device *dev);

	struct subsys_private *p;
	struct lock_class_key lock_key;

	bool need_parent_lock;
};

/* bus_register removed - never called */
/* bus_unregister removed - never called */
/* struct bus_attribute removed - never used */

/* BUS_ATTR_RW, BUS_ATTR_WO, device_match_name, device_match_of_node, device_match_fwnode,
   device_match_acpi_dev, device_match_acpi_handle, device_match_any removed - unused */
int device_match_devt(struct device *dev, const void *pdevt);


/* bus_find_device_by_name, bus_find_device_by_of_node, bus_find_device_by_fwnode,
 * bus_find_device_by_devt, bus_find_next_device, bus_find_device_by_acpi_dev,
 * subsys_find_device_by_id removed - unused */

int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *));

/* BUS_NOTIFY_* macros removed - unused */

#endif
