
#include <linux/device.h>
#include <linux/init.h>
#include "base.h"

/* Removed: bus_get + the driver_ktype kobj_type (drv_attr_show/store,
   driver_sysfs_ops, driver_release) - only used by the dead bus_add_driver */

/* Removed: bus_attr_show/store + bus_sysfs_ops + bus_create_file/bus_remove_file
   - no bus_attribute is ever registered (the create/remove helpers were no-op
   stubs), so the sysfs_ops dispatcher and the bus_attribute show/store callbacks
   were never reached. */

/* Removed: bus_release + bus_ktype + bus_uevent_filter + bus_uevent_ops +
   bus_kset - no bus is ever registered and nothing is added under the "bus"
   kset, so the kset created by buses_init was never read and its uevent
   filter / ktype release callbacks were never reached. buses_init is now a
   no-op (see below). */

/* Removed: next_driver + bus_for_each_drv - klist_drivers is always empty (no
   driver_register), so the only caller (__device_attach, also removed) iterated
   nothing. */

/* Removed: bus_remove_device + bus_put - device_del (its only caller) is gone;
   no device is ever removed from a bus on this build. */

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
	return 0;
}
