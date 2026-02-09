
#include <linux/async.h>
#include <linux/device/bus.h>
#include <linux/device.h>
/* linux/module.h removed - no module features used */
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "base.h"

static struct kset *system_kset;

/* to_bus_attr, to_drv_attr macros removed - only used by removed ktype functions */
/* DRIVER_ATTR_IGNORE_LOCKDEP removed - only used for bind/unbind which were removed */

/* bus_get removed - never called */
/* bus_put inlined into bus_remove_device - single caller */

/* drv_attr_show, drv_attr_store, driver_sysfs_ops, driver_release,
   driver_ktype, bus_attr_show, bus_attr_store, bus_sysfs_ops,
   bus_release, bus_ktype removed - never used (~77 LOC) */

/* bus_uevent_filter, bus_uevent_ops removed - callbacks never invoked */

static struct kset *bus_kset;

/* unbind_store, bind_store, driver_attr_unbind, driver_attr_bind removed -
   driver_create_file is a stub that doesn't actually create files */

/* next_device, bus_for_each_dev, bus_find_device removed - no callers */

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

	/* interfaces loop removed - subsys_interface_register never called */
}

/* Simplified: sysfs functions are stubs */
void bus_remove_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;

	if (!bus)
		return;

	/* interfaces loop removed - subsys_interface_register never called */

	if (klist_node_attached(&dev->p->knode_bus))
		klist_del(&dev->p->knode_bus);

	device_release_driver(dev);
	/* bus_put inlined */
	if (dev->bus)
		kset_put(&dev->bus->p->subsys);
}

/* uevent_store, driver_attr_uevent, add_bind_files, remove_bind_files removed -
   driver_create_file was removed so these attributes are never used */

/* bus_add_driver removed - never called (only caller was driver_register) */
/* bus_remove_driver removed - never called (~14 LOC) */

/* klist_devices_get and klist_devices_put removed - only caller was bus_add_driver (~15 LOC) */

/* bus_register removed - never called */
/* bus_unregister, subsys_register, subsys_system_register removed - never called (~51 LOC) */

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
