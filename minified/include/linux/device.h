
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/module.h>
struct device;

struct bus_type {
	const char		*name;
	int (*match)(struct device *dev, struct device_driver *drv);
	struct subsys_private *p;
};

struct class {
	const char		*name;
	struct module		*owner;
	void (*dev_release)(struct device *dev);
	struct subsys_private *p;
};

struct device_driver {
	const char		*name;
	struct bus_type		*bus;
	struct module		*owner;
	struct driver_private *p;
};

struct device {
	struct kobject kobj;
	struct device		*parent;
	const char		*init_name;
	struct bus_type	*bus;
	struct device_driver *driver;
	struct mutex		mutex;
	dev_t			devt;
	spinlock_t		devres_lock;
	struct list_head	devres_head;
	struct class		*class;
	void	(*release)(struct device *dev);
};

#endif
