
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


/* Removed: bus_add_groups/bus_remove_groups/klist_devices_get/klist_devices_put
   + bus_register/bus_unregister + subsys_register/subsys_system_register +
   system_root_device_release - the cpu_subsys was the only bus ever registered
   (via cpu_dev_init); with that gone no bus is registered, so the entire
   bus-registration machinery is dead. The "system" kset it parented is likewise
   unused. */

int __init buses_init(void)
{
	bus_kset = kset_create_and_add("bus", &bus_uevent_ops, NULL);
	if (!bus_kset)
		return -ENOMEM;

	return 0;
}
