 
 
#include <linux/notifier.h>

 
/* Trimmed: interfaces, drivers_kset, klist_drivers, bus_notifier and
   drivers_autoprobe were write-only on this build - no driver registers
   (subsys_interface_register / class_interface_register are gone, so the
   interfaces list and the drivers klist/kset stay empty) and nothing ever
   registers on bus_notifier. */
/* class back-pointer removed - was write-only (set in __class_register,
   never read by any consumer). */
struct subsys_private {
	struct kset subsys;
};
#define to_subsys_private(obj) container_of(obj, struct subsys_private, subsys.kobj)

/* Removed: struct driver_private - the driver-side klist/kobj is never built
   (no driver_register/bus_add_driver), and driver->p is never dereferenced. */

/* Removed: klist_devices/klist_children/knode_parent/knode_class + their
   to_device_private_* macros - the klist get/put callbacks they fed were
   write-only (klist_add/del/get/put don't exist in this build, so the stored
   ->get/->put fn-ptrs were never invoked and the nodes never linked). */
struct device_private {
	struct device *device;
};

 
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);

/* virtual_device_parent removed - unused */

/* Removed: bus_remove_device + driver_deferred_probe_del - device teardown path
   is gone (device_del removed). */
/* Removed: driver_match_device - 0 callers (driver-side bind machinery is gone). */


extern struct kset *devices_kset;


/* devtmpfs_create_node / devtmpfs_delete_node removed - unused */

/* software_node_notify / software_node_notify_remove removed - unused */
