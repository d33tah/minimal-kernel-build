
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/module.h>

struct device {
	struct kobject kobj;
	struct device *parent;
	const char *init_name;
	struct mutex mutex;
	dev_t devt;
	spinlock_t devres_lock;
	struct list_head devres_head;
	void (*release)(struct device *dev);
};

#endif
