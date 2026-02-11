 
 
#include <linux/notifier.h>

 
struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	/* interfaces removed - list never populated */
	struct mutex mutex;

	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	/* bus_notifier removed - never used */
	unsigned int drivers_autoprobe:1;
	struct bus_type *bus;

	struct kset glue_dirs;
	struct class *class;
};
/* to_subsys_private macro removed - never called */

struct driver_private {
	struct kobject kobj;
	struct klist klist_devices;
	struct klist_node knode_bus;
	/* mkobj removed - never used */
	struct device_driver *driver;
};
/* to_driver macro removed - never called */

struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_driver;
	struct klist_node knode_bus;
	struct klist_node knode_class;
	struct list_head deferred_probe;
	/* async_driver removed - never used */
	char *deferred_probe_reason;
	struct device *device;
	u8 dead:1;
};
/* to_device_private_parent, to_device_private_driver, to_device_private_bus,
   to_device_private_class macros removed - never called */

 
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
/* firmware_init, hypervisor_init removed - never called */
/* platform_bus_init, cpu_dev_init, container_dev_init, auxiliary_bus_init removed - never called */

/* virtual_device_parent removed - unused */

/* bus_add_device removed - never called */
extern void bus_probe_device(struct device *dev);

/* bus_add_driver, bus_remove_driver, driver_detach removed - never called */

/* device_set_deferred_probe_reason removed - orphan extern declaration */
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}

/* driver_add_groups, driver_remove_groups, device_driver_detach removed - unused */

/* make_class_name removed - unused */

/* devres_release_all, device_block_probing, device_unblock_probing removed - unused */
/* deferred_probe_extend_timeout removed - never called */

/* devices_kset_move_last removed - unused */
extern struct kset *devices_kset;

/* module_add_driver, module_remove_driver, devtmpfs_init removed - never called */

/* device_links_*, fw_devlink_drivers_done, device_pm_move_to_tail removed - empty stubs or inlined */

/* devtmpfs_create_node, devtmpfs_delete_node, software_node_notify, software_node_notify_remove removed - never called */
