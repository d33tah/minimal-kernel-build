 
 
#include <linux/notifier.h>

 
/* Trimmed: interfaces, drivers_kset, klist_drivers, bus_notifier and
   drivers_autoprobe were write-only on this build - no driver registers
   (subsys_interface_register / class_interface_register are gone, so the
   interfaces list and the drivers klist/kset stay empty) and nothing ever
   registers on bus_notifier. */
struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct mutex mutex;

	struct klist klist_devices;
	struct bus_type *bus;

	struct kset glue_dirs;
	struct class *class;
};
#define to_subsys_private(obj) container_of(obj, struct subsys_private, subsys.kobj)

/* Removed: struct driver_private - the driver-side klist/kobj is never built
   (no driver_register/bus_add_driver), and driver->p is never dereferenced. */


struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_bus;
	struct klist_node knode_class;
	struct list_head deferred_probe;
	struct device *device;
	u8 dead:1;
};
#define to_device_private_parent(obj)	\
	container_of(obj, struct device_private, knode_parent)
#define to_device_private_bus(obj)	\
	container_of(obj, struct device_private, knode_bus)
#define to_device_private_class(obj)	\
	container_of(obj, struct device_private, knode_class)

 
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
static inline int firmware_init(void) { return 0; }
static inline int hypervisor_init(void) { return 0; }
extern void cpu_dev_init(void);
static inline void container_dev_init(void) { }
static inline void auxiliary_bus_init(void) { }

/* virtual_device_parent removed - unused */

extern void bus_probe_device(struct device *dev);
extern void bus_remove_device(struct device *dev);

extern void driver_deferred_probe_del(struct device *dev);
/* Removed: driver_match_device - 0 callers (driver-side bind machinery is gone). */
extern int devres_release_all(struct device *dev);


extern struct kset *devices_kset;

static inline void module_add_driver(struct module *mod,
				     struct device_driver *drv) { }
static inline void module_remove_driver(struct device_driver *drv) { }

static inline int devtmpfs_init(void) { return 0; }

 
/* devtmpfs_create_node removed - unused */
static inline int devtmpfs_delete_node(struct device *dev) { return 0; }

/* software_node_notify removed - unused */
void software_node_notify_remove(struct device *dev);
