 
#include <linux/notifier.h>

struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct mutex mutex;

	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	unsigned int drivers_autoprobe:1;
	struct bus_type *bus;

	struct kset glue_dirs;
	struct class *class;
};

struct driver_private {
	struct kobject kobj;
	struct klist klist_devices;
	struct klist_node knode_bus;
	struct device_driver *driver;
};

struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_driver;
	struct klist_node knode_bus;
	struct klist_node knode_class;
	struct list_head deferred_probe;
	struct device *device;
	u8 dead:1;
};
extern int devices_init(void);
extern int buses_init(void);

extern void bus_probe_device(struct device *dev);

static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}

extern struct kset *devices_kset;

