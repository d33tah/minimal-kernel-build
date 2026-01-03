 
 
#include <linux/notifier.h>

 
struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct list_head interfaces;
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
#define to_subsys_private(obj) container_of(obj, struct subsys_private, subsys.kobj)

struct driver_private {
	struct kobject kobj;
	struct klist klist_devices;
	struct klist_node knode_bus;
	struct module_kobject *mkobj;
	struct device_driver *driver;
};
#define to_driver(obj) container_of(obj, struct driver_private, kobj)

 
struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_driver;
	struct klist_node knode_bus;
	struct klist_node knode_class;
	struct list_head deferred_probe;
	struct device_driver *async_driver;
	char *deferred_probe_reason;
	struct device *device;
	u8 dead:1;
};
#define to_device_private_parent(obj)	\
	container_of(obj, struct device_private, knode_parent)
#define to_device_private_driver(obj)	\
	container_of(obj, struct device_private, knode_driver)
#define to_device_private_bus(obj)	\
	container_of(obj, struct device_private, knode_bus)
#define to_device_private_class(obj)	\
	container_of(obj, struct device_private, knode_class)

 
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
/* firmware_init, hypervisor_init removed - never called */
extern int platform_bus_init(void);
/* cpu_dev_init, container_dev_init, auxiliary_bus_init removed - never called */

/* virtual_device_parent removed - unused */

extern int bus_add_device(struct device *dev);
extern void bus_probe_device(struct device *dev);
extern void bus_remove_device(struct device *dev);

extern int bus_add_driver(struct device_driver *drv);
/* bus_remove_driver, driver_detach removed - never called */
extern void device_release_driver_internal(struct device *dev,
					   struct device_driver *drv,
					   struct device *parent);

extern void driver_deferred_probe_del(struct device *dev);
/* device_set_deferred_probe_reason removed - orphan extern declaration */
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}

extern int driver_add_groups(struct device_driver *drv,
			     const struct attribute_group **groups);
extern void driver_remove_groups(struct device_driver *drv,
				 const struct attribute_group **groups);
/* device_driver_detach removed - unused */

/* make_class_name removed - unused */

/* devres_release_all, device_block_probing, device_unblock_probing removed - unused */
extern void deferred_probe_extend_timeout(void);

/* devices_kset_move_last removed - unused */
extern struct kset *devices_kset;

static inline void module_add_driver(struct module *mod,
				     struct device_driver *drv) { }
/* module_remove_driver, devtmpfs_init - never called */

/* device_links_*, fw_devlink_drivers_done removed - empty stubs or inlined */

void device_pm_move_to_tail(struct device *dev);

/* devtmpfs_create_node, devtmpfs_delete_node, software_node_notify, software_node_notify_remove removed - never called */
