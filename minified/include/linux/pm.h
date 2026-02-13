
#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/spinlock.h>

struct device;

struct dev_pm_ops {
};

struct dev_pm_info {
	u32			driver_flags;
};

struct dev_pm_domain {
	struct dev_pm_ops	ops;
	int (*start)(struct device *dev);
	void (*detach)(struct device *dev, bool power_off);
	int (*activate)(struct device *dev);
	void (*sync)(struct device *dev);
	void (*dismiss)(struct device *dev);
};

#endif
