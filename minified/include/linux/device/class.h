
#ifndef _DEVICE_CLASS_H_
#define _DEVICE_CLASS_H_

#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/pm.h>
#include <linux/device/bus.h>

struct device;
struct fwnode_handle;

struct class {
	const char		*name;
	struct module		*owner;

	/* class_groups, dev_groups, dev_kobj, dev_uevent, devnode, class_release, shutdown_pre removed */
	void (*dev_release)(struct device *dev);

	/* ns_type, namespace, get_ownership, pm removed - never set/accessed */

	struct subsys_private *p;
};

/* class_dev_iter, class_find_device, class_find_device_by_devt,
   device_match_devt all removed - never called */

#endif	 
