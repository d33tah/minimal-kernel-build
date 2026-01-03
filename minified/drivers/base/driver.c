
#include <linux/device/driver.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "base.h"

/* driver_find_device removed - never called */

/* Stub: sysfs functions are stubs - minimal driver file/group management */
int driver_create_file(struct device_driver *drv,
		       const struct driver_attribute *attr)
{
	return drv ? 0 : -EINVAL;
}

void driver_remove_file(struct device_driver *drv,
			const struct driver_attribute *attr)
{
}

int driver_add_groups(struct device_driver *drv,
		      const struct attribute_group **groups)
{
	return 0;
}

void driver_remove_groups(struct device_driver *drv,
			  const struct attribute_group **groups)
{
}

int driver_register(struct device_driver *drv)
{
	int ret;

	if (!drv->bus->p) {
		pr_err("Driver '%s' was unable to register with bus_type '%s' because the bus was not initialized.\n",
		       drv->name, drv->bus->name);
		return -EINVAL;
	}

	/* driver_find always returns NULL (kset_find_obj stubbed) */

	ret = bus_add_driver(drv);
	if (ret)
		return ret;
	driver_add_groups(drv, drv->groups);
	/* error check removed - driver_add_groups always returns 0 */
	kobject_uevent(&drv->p->kobj, KOBJ_ADD);
	deferred_probe_extend_timeout();

	return ret;
}

/* driver_find removed - kset_find_obj always returns NULL */
