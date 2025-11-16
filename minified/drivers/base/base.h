 
 
#include <linux/notifier.h>

 
struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct list_head interfaces;
	struct mutex mutex;

	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	struct blocking_notifier_head bus_notifier;
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
static inline int firmware_init(void) { return 0; }
static inline int hypervisor_init(void) { return 0; }
extern int platform_bus_init(void);
extern void cpu_dev_init(void);
static inline void container_dev_init(void) { }
static inline void auxiliary_bus_init(void) { }

struct kobject *virtual_device_parent(struct device *dev);

extern int bus_add_device(struct device *dev);
extern void bus_probe_device(struct device *dev);
extern void bus_remove_device(struct device *dev);

extern int bus_add_driver(struct device_driver *drv);
extern void bus_remove_driver(struct device_driver *drv);
extern void device_release_driver_internal(struct device *dev,
					   struct device_driver *drv,
					   struct device *parent);

extern void driver_detach(struct device_driver *drv);
extern void driver_deferred_probe_del(struct device *dev);
extern void device_set_deferred_probe_reason(const struct device *dev,
					     struct va_format *vaf);
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}
extern bool driver_allows_async_probing(struct device_driver *drv);

extern int driver_add_groups(struct device_driver *drv,
			     const struct attribute_group **groups);
extern void driver_remove_groups(struct device_driver *drv,
				 const struct attribute_group **groups);
void device_driver_detach(struct device *dev);

extern char *make_class_name(const char *name, struct kobject *kobj);

extern int devres_release_all(struct device *dev);
extern void device_block_probing(void);
extern void device_unblock_probing(void);
extern void deferred_probe_extend_timeout(void);

 
extern struct kset *devices_kset;
extern void devices_kset_move_last(struct device *dev);

static inline void module_add_driver(struct module *mod,
				     struct device_driver *drv) { }
static inline void module_remove_driver(struct device_driver *drv) { }

static inline int devtmpfs_init(void) { return 0; }

 
extern int device_links_read_lock(void);
extern void device_links_read_unlock(int idx);
extern int device_links_read_lock_held(void);
extern int device_links_check_suppliers(struct device *dev);
extern void device_links_force_bind(struct device *dev);
extern void device_links_driver_bound(struct device *dev);
extern void device_links_driver_cleanup(struct device *dev);
extern void device_links_no_driver(struct device *dev);
extern bool device_links_busy(struct device *dev);
extern void device_links_unbind_consumers(struct device *dev);
extern void fw_devlink_drivers_done(void);

 
void device_pm_move_to_tail(struct device *dev);

static inline int devtmpfs_create_node(struct device *dev) { return 0; }
static inline int devtmpfs_delete_node(struct device *dev) { return 0; }

void software_node_notify(struct device *dev);
void software_node_notify_remove(struct device *dev);
