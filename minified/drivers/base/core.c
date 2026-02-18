
#include <linux/slab.h>
#include <linux/backing-dev.h>

#include "base.h"

/* Merged from init.c */
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	kset_create_and_add("class", NULL);
}

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

int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", NULL);
	return 0;
}
