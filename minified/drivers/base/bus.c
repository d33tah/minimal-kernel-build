
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "base.h"

static struct kset *system_kset;

/* drv_attr_show, drv_attr_store, driver_sysfs_ops, driver_release,
   driver_ktype, bus_attr_show, bus_attr_store, bus_sysfs_ops,
   bus_release, bus_ktype removed - never used (~77 LOC) */

static struct kset *bus_kset;

/* unbind_store, bind_store, driver_attr_unbind, driver_attr_bind removed -
   driver_create_file is a stub that doesn't actually create files */

static struct device_driver *next_driver(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct driver_private *drv_priv;

	if (n) {
		drv_priv = container_of(n, struct driver_private, knode_bus);
		return drv_priv->driver;
	}
	return NULL;
}

int bus_for_each_drv(struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *))
{
	struct klist_iter i;
	struct device_driver *drv;
	int error = 0;

	if (!bus)
		return -EINVAL;

	klist_iter_init_node(&bus->p->klist_drivers, &i,
			     start ? &start->p->knode_bus : NULL);
	while ((drv = next_driver(&i)) && !error)
		error = fn(drv, data);
	klist_iter_exit(&i);
	return error;
}

void bus_probe_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;

	if (!bus)
		return;

	if (bus->p->drivers_autoprobe)
		device_initial_probe(dev);
}

/* Simplified: sysfs functions are stubs */
/* uevent_store, driver_attr_uevent, add_bind_files, remove_bind_files removed -
   driver_create_file was removed so these attributes are never used */

int __init buses_init(void)
{
	bus_kset = kset_create_and_add("bus", NULL);
	if (!bus_kset)
		return -ENOMEM;

	system_kset = kset_create_and_add("system", &devices_kset->kobj);
	if (!system_kset)
		return -ENOMEM;

	return 0;
}
