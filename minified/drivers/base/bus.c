
#include <linux/async.h>
#include <linux/device/bus.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "base.h"
#include "power/power.h"

static struct kset *system_kset;

static void bus_put(struct bus_type *bus)
{
	if (bus)
		kset_put(&bus->p->subsys);
}

/* Removed: bus_get + the driver_ktype kobj_type (drv_attr_show/store,
   driver_sysfs_ops, driver_release) - only used by the dead bus_add_driver */

/* Removed: bus_attr_show/store + bus_sysfs_ops + bus_create_file/bus_remove_file
   - no bus_attribute is ever registered (the create/remove helpers were no-op
   stubs), so the sysfs_ops dispatcher and the bus_attribute show/store callbacks
   were never reached. */

static void bus_release(struct kobject *kobj)
{
	struct subsys_private *priv = to_subsys_private(kobj);
	struct bus_type *bus = priv->bus;

	kfree(priv);
	bus->p = NULL;
}

static struct kobj_type bus_ktype = {
	.release	= bus_release,
};

static int bus_uevent_filter(struct kobject *kobj)
{
	const struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &bus_ktype)
		return 1;
	return 0;
}

static const struct kset_uevent_ops bus_uevent_ops = {
	.filter = bus_uevent_filter,
};

static struct kset *bus_kset;

/* Removed: next_driver + bus_for_each_drv - klist_drivers is always empty (no
   driver_register), so the only caller (__device_attach, also removed) iterated
   nothing. */

/* Simplified: sysfs functions are stubs, so no error paths needed.
   device_initial_probe() was a no-op (empty driver klist + stubbed PM runtime)
   and bus->p->interfaces is always empty (subsys_interface_register is gone),
   so both the autoprobe and the add_dev loop are dead. */
void bus_probe_device(struct device *dev)
{
}

/* Simplified: sysfs functions are stubs; bus->p->interfaces is always empty
   (subsys_interface_register is gone), knode_bus is never klist_add'd, and no
   driver ever binds so device_release_driver was a no-op (now removed). */
void bus_remove_device(struct device *dev)
{
	struct bus_type *bus = dev->bus;

	if (!bus)
		return;

	bus_put(dev->bus);
}

/* Removed: drivers_probe/drivers_autoprobe bus_attributes + add_probe_files/
   remove_probe_files - the sysfs files were never created (bus_create_file was
   a no-op stub) so the attributes' show/store were never dispatched. */

/* Removed: bus_add_driver + bus_remove_driver - no driver registers in this
   minimal kernel, so driver_register (their only caller) is gone */


/* Stub: sysfs functions are stubs */
static int bus_add_groups(struct bus_type *bus,
			  const struct attribute_group **groups)
{
	return 0;
}

static void bus_remove_groups(struct bus_type *bus,
			      const struct attribute_group **groups)
{
}

static void klist_devices_get(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	get_device(dev);
}

static void klist_devices_put(struct klist_node *n)
{
	struct device_private *dev_prv = to_device_private_bus(n);
	struct device *dev = dev_prv->device;

	put_device(dev);
}

/* Removed: bus_uevent_store + bus_attr_uevent - the "uevent" sysfs file was
   never created (bus_create_file no-op), so the store callback was never run. */

int bus_register(struct bus_type *bus)
{
	int retval;
	struct subsys_private *priv;
	struct lock_class_key *key = &bus->lock_key;

	priv = kzalloc(sizeof(struct subsys_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = bus;
	bus->p = priv;

	BLOCKING_INIT_NOTIFIER_HEAD(&priv->bus_notifier);

	retval = kobject_set_name(&priv->subsys.kobj, "%s", bus->name);
	if (retval)
		goto out;

	priv->subsys.kobj.kset = bus_kset;
	priv->subsys.kobj.ktype = &bus_ktype;
	priv->drivers_autoprobe = 1;

	retval = kset_register(&priv->subsys);
	if (retval)
		goto out;

	priv->devices_kset = kset_create_and_add("devices", NULL,
						 &priv->subsys.kobj);
	if (!priv->devices_kset) {
		retval = -ENOMEM;
		goto bus_devices_fail;
	}

	priv->drivers_kset = kset_create_and_add("drivers", NULL,
						 &priv->subsys.kobj);
	if (!priv->drivers_kset) {
		retval = -ENOMEM;
		goto bus_drivers_fail;
	}

	INIT_LIST_HEAD(&priv->interfaces);
	__mutex_init(&priv->mutex, "subsys mutex", key);
	klist_init(&priv->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&priv->klist_drivers, NULL, NULL);

	retval = bus_add_groups(bus, bus->bus_groups);
	if (retval)
		goto bus_groups_fail;

	return 0;

bus_groups_fail:
	kset_unregister(bus->p->drivers_kset);
bus_drivers_fail:
	kset_unregister(bus->p->devices_kset);
bus_devices_fail:
	kset_unregister(&bus->p->subsys);
out:
	kfree(bus->p);
	bus->p = NULL;
	return retval;
}

void bus_unregister(struct bus_type *bus)
{
	if (bus->dev_root)
		device_unregister(bus->dev_root);
	bus_remove_groups(bus, bus->bus_groups);
	kset_unregister(bus->p->drivers_kset);
	kset_unregister(bus->p->devices_kset);
	kset_unregister(&bus->p->subsys);
}

/* Removed: bus_get_device_klist, bus_unregister_notifier, bus_get_kset, bus_sort_breadthfirst,
   subsys_dev_iter_init/next/exit, subsys_interface_register/unregister - no external callers */

static void system_root_device_release(struct device *dev)
{
	kfree(dev);
}

static int subsys_register(struct bus_type *subsys,
			   const struct attribute_group **groups,
			   struct kobject *parent_of_root)
{
	struct device *dev;
	int err;

	err = bus_register(subsys);
	if (err < 0)
		return err;

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto err_dev;
	}

	err = dev_set_name(dev, "%s", subsys->name);
	if (err < 0)
		goto err_name;

	dev->kobj.parent = parent_of_root;
	dev->groups = groups;
	dev->release = system_root_device_release;

	err = device_register(dev);
	if (err < 0)
		goto err_dev_reg;

	subsys->dev_root = dev;
	return 0;

err_dev_reg:
	put_device(dev);
	dev = NULL;
err_name:
	kfree(dev);
err_dev:
	bus_unregister(subsys);
	return err;
}

int subsys_system_register(struct bus_type *subsys,
			   const struct attribute_group **groups)
{
	return subsys_register(subsys, groups, &system_kset->kobj);
}


int __init buses_init(void)
{
	bus_kset = kset_create_and_add("bus", &bus_uevent_ops, NULL);
	if (!bus_kset)
		return -ENOMEM;

	system_kset = kset_create_and_add("system", NULL, &devices_kset->kobj);
	if (!system_kset)
		return -ENOMEM;

	return 0;
}
