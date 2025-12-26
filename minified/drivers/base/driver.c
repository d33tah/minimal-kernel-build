
#include <linux/device/driver.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "base.h"

/* driver_find_device stubbed - never called */
struct device *driver_find_device(struct device_driver *drv,
				  struct device *start, const void *data,
				  int (*match)(struct device *dev,
					       const void *data))
{
	return NULL;
}

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
	struct device_driver *other;

	if (!drv->bus->p) {
		pr_err("Driver '%s' was unable to register with bus_type '%s' because the bus was not initialized.\n",
		       drv->name, drv->bus->name);
		return -EINVAL;
	}

	other = driver_find(drv->name, drv->bus);
	if (other) {
		pr_err("Error: Driver '%s' is already registered, "
		       "aborting...\n",
		       drv->name);
		return -EBUSY;
	}

	ret = bus_add_driver(drv);
	if (ret)
		return ret;
	ret = driver_add_groups(drv, drv->groups);
	if (ret) {
		bus_remove_driver(drv);
		return ret;
	}
	kobject_uevent(&drv->p->kobj, KOBJ_ADD);
	deferred_probe_extend_timeout();

	return ret;
}

struct device_driver *driver_find(const char *name, struct bus_type *bus)
{
	struct kobject *k = kset_find_obj(bus->p->drivers_kset, name);
	struct driver_private *priv;

	if (k) {
		kobject_put(k);
		priv = to_driver(k);
		return priv->driver;
	}
	return NULL;
}
