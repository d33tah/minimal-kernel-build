
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sysfs.h>
#include <linux/backing-dev.h>

#include "base.h"

/* Merged from init.c */
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	/* classes_init inlined - creates "class" kset */
	kset_create_and_add("class", NULL);
}

/* Stub: online sysfs attributes simplified for minimal kernel */

/* device_remove_file_self, device_create_bin_file, device_remove_bin_file - no callers */

struct kset *devices_kset;

static DEFINE_MUTEX(gdp_mutex);

struct device *get_device(struct device *dev)
{
	return dev ? kobj_to_dev(kobject_get(&dev->kobj)) : NULL;
}

void put_device(struct device *dev)
{
	if (dev)
		kobject_put(&dev->kobj);
}

/* Simplified - kobject_create_and_add returns NULL (stub), return ignored */
int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", NULL);
	return 0;
}

/* device_create_release, device_create_groups_vargs, device_create,
   device_destroy removed - no callers */
