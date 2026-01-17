
#include <linux/device/driver.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "base.h"

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
	/* driver_add_groups call removed - was a stub returning 0 */
	kobject_uevent(&drv->p->kobj, KOBJ_ADD);
	deferred_probe_extend_timeout();

	return ret;
}

/* driver_find removed - kset_find_obj always returns NULL */
