
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fwnode.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/swiotlb.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/dma-map-ops.h> 

#include "base.h"
/* physical_location.h inlined */
static inline bool dev_add_physical_location(struct device *dev) { return false; }
static const struct attribute_group dev_attr_physical_location_group = {};
/* end physical_location.h */
#include "power/power.h"

static LIST_HEAD(deferred_sync);
static DEFINE_MUTEX(fwnode_link_lock);
/* fw_devlink_is_permissive forward decl removed - unused */
static bool fw_devlink_drv_reg_done;

/* Removed: fwnode_link_add, fwnode_links_purge, fw_devlink_purge_absent_suppliers - no callers */

static DEFINE_MUTEX(device_links_lock);
DEFINE_STATIC_SRCU(device_links_srcu);

static inline void device_links_write_lock(void)
{
	mutex_lock(&device_links_lock);
}

static inline void device_links_write_unlock(void)
{
	mutex_unlock(&device_links_lock);
}

static int device_links_read_lock(void) __acquires(&device_links_srcu)
{
	return srcu_read_lock(&device_links_srcu);
}

static void device_links_read_unlock(int idx) __releases(&device_links_srcu)
{
	srcu_read_unlock(&device_links_srcu, idx);
}

/* device_links_read_lock_held removed - unused */

static void device_link_synchronize_removal(void)
{
	synchronize_srcu(&device_links_srcu);
}

/* device_is_dependent removed - unused */

static int device_reorder_to_tail(struct device *dev, void *not_used)
{
	/* Minimal stub */
	(void)dev;
	(void)not_used;
	return 0;
}

void device_pm_move_to_tail(struct device *dev)
{
	int idx;

	idx = device_links_read_lock();
	device_pm_lock();
	device_reorder_to_tail(dev, NULL);
	device_pm_unlock();
	device_links_read_unlock(idx);
}

#define to_devlink(dev)	container_of((dev), struct device_link, link_dev)

/* Stub: device link sysfs attributes not needed for minimal kernel */
static struct attribute *devlink_attrs[] = { NULL };
ATTRIBUTE_GROUPS(devlink);

static void device_link_release_fn(struct work_struct *work)
{
	struct device_link *link = container_of(work, struct device_link, rm_work);

	
	device_link_synchronize_removal();

	pm_runtime_release_supplier(link);
	
	if (link->supplier_preactivated)
		pm_runtime_put_noidle(link->supplier);

	pm_request_idle(link->supplier);

	put_device(link->consumer);
	put_device(link->supplier);
	kfree(link);
}

static void devlink_dev_release(struct device *dev)
{
	struct device_link *link = to_devlink(dev);

	INIT_WORK(&link->rm_work, device_link_release_fn);
	
	queue_work(system_long_wq, &link->rm_work);
}

static struct class devlink_class = {
	.name = "devlink",
	.owner = THIS_MODULE,
	.dev_groups = devlink_groups,
	.dev_release = devlink_dev_release,
};

static int devlink_add_symlinks(struct device *dev,
				struct class_interface *class_intf)
{
	/* Stubbed: device link symlinks not needed for minimal boot */
	return 0;
}

static void devlink_remove_symlinks(struct device *dev,
				   struct class_interface *class_intf)
{
	struct device_link *link = to_devlink(dev);
	size_t len;
	struct device *sup = link->supplier;
	struct device *con = link->consumer;
	char *buf;

	sysfs_remove_link(&link->link_dev.kobj, "consumer");
	sysfs_remove_link(&link->link_dev.kobj, "supplier");

	len = max(strlen(dev_bus_name(sup)) + strlen(dev_name(sup)),
		  strlen(dev_bus_name(con)) + strlen(dev_name(con)));
	len += strlen(":");
	len += strlen("supplier:") + 1;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		WARN(1, "Unable to properly free device link symlinks!\n");
		return;
	}

	if (device_is_registered(con)) {
		snprintf(buf, len, "supplier:%s:%s", dev_bus_name(sup), dev_name(sup));
		sysfs_remove_link(&con->kobj, buf);
	}
	snprintf(buf, len, "consumer:%s:%s", dev_bus_name(con), dev_name(con));
	sysfs_remove_link(&sup->kobj, buf);
	kfree(buf);
}

static struct class_interface devlink_class_intf = {
	.class = &devlink_class,
	.add_dev = devlink_add_symlinks,
	.remove_dev = devlink_remove_symlinks,
};

static int __init devlink_class_init(void)
{
	int ret;

	ret = class_register(&devlink_class);
	if (ret)
		return ret;

	ret = class_interface_register(&devlink_class_intf);
	if (ret)
		class_unregister(&devlink_class);

	return ret;
}
postcore_initcall(devlink_class_init);

#define DL_MANAGED_LINK_FLAGS (DL_FLAG_AUTOREMOVE_CONSUMER | \
			       DL_FLAG_AUTOREMOVE_SUPPLIER | \
			       DL_FLAG_AUTOPROBE_CONSUMER  | \
			       DL_FLAG_SYNC_STATE_ONLY | \
			       DL_FLAG_INFERRED)

#define DL_ADD_VALID_FLAGS (DL_MANAGED_LINK_FLAGS | DL_FLAG_STATELESS | \
			    DL_FLAG_PM_RUNTIME | DL_FLAG_RPM_ACTIVE)

/* device_link_add removed - only returns NULL stub, never called */

/* Stub: no device links since device_link_add returns NULL */
int device_links_check_suppliers(struct device *dev)
{
	dev->links.status = DL_DEV_PROBING;
	return 0;
}

/* Stub: waiting_for_supplier sysfs attribute simplified for minimal kernel */
static ssize_t waiting_for_supplier_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "0\n");
}
static DEVICE_ATTR_RO(waiting_for_supplier);

/* device_links_force_bind, device_links_driver_bound removed - unused */

void device_links_no_driver(struct device *dev)
{
	/* Minimal stub */
	(void)dev;
}

void device_links_driver_cleanup(struct device *dev)
{
	/* Minimal stub */
	(void)dev;
}

bool device_links_busy(struct device *dev)
{
	/* Minimal stub: never busy */
	(void)dev;
	return false;
}

void device_links_unbind_consumers(struct device *dev)
{
	/* Minimal stub */
	(void)dev;
}

static void device_links_purge(struct device *dev)
{
	/* Stub: no device links to purge since device_link_add returns NULL */
}

#define FW_DEVLINK_FLAGS_PERMISSIVE	(DL_FLAG_INFERRED | \
					 DL_FLAG_SYNC_STATE_ONLY)
#define FW_DEVLINK_FLAGS_ON		(DL_FLAG_INFERRED | \
					 DL_FLAG_AUTOPROBE_CONSUMER)
#define FW_DEVLINK_FLAGS_RPM		(FW_DEVLINK_FLAGS_ON | \
					 DL_FLAG_PM_RUNTIME)

static u32 fw_devlink_flags = FW_DEVLINK_FLAGS_ON;
/* Stub: fw_devlink= cmdline option not needed for minimal kernel */
static int __init fw_devlink_setup(char *arg) { return 0; }
early_param("fw_devlink", fw_devlink_setup);

/* Stub: fw_devlink.strict= cmdline option not needed for minimal kernel */
static int __init fw_devlink_strict_setup(char *arg) { return 0; }
early_param("fw_devlink.strict", fw_devlink_strict_setup);

static u32 fw_devlink_get_flags(void)
{
	return fw_devlink_flags;
}

/* fw_devlink_is_strict removed - no callers */

/* fw_devlink_parse_fwnode, fw_devlink_parse_fwtree removed - unused */

/* Stub: firmware device link functions not needed for minimal kernel */
void fw_devlink_drivers_done(void)
{
	fw_devlink_drv_reg_done = true;
}

/* platform_notify, platform_notify_remove removed - never assigned */
static struct kobject *dev_kobj;
struct kobject *sysfs_dev_char_kobj;
struct kobject *sysfs_dev_block_kobj;

static DEFINE_MUTEX(device_hotplug_lock);

/* Removed: lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs - no callers */

static inline int device_is_not_partition(struct device *dev)
{
	return 1;
}

static void device_platform_notify_remove(struct device *dev)
{
	acpi_device_notify_remove(dev);
	software_node_notify_remove(dev);
	/* platform_notify_remove call removed - never assigned */
}

/* dev_driver_string removed - no callers */

#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

static ssize_t dev_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = kobj_to_dev(kobj);
	ssize_t ret = -EIO;

	if (dev_attr->show)
		ret = dev_attr->show(dev, dev_attr, buf);
	if (ret >= (ssize_t)PAGE_SIZE) {
		printk("dev_attr_show: %pS returned bad count\n",
				dev_attr->show);
	}
	return ret;
}

static ssize_t dev_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct device_attribute *dev_attr = to_dev_attr(attr);
	struct device *dev = kobj_to_dev(kobj);
	ssize_t ret = -EIO;

	if (dev_attr->store)
		ret = dev_attr->store(dev, dev_attr, buf, count);
	return ret;
}

static const struct sysfs_ops dev_sysfs_ops = {
	.show	= dev_attr_show,
	.store	= dev_attr_store,
};

static void device_release(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	struct device_private *p = dev->p;

	
	devres_release_all(dev);

	kfree(dev->dma_range_map);

	if (dev->release)
		dev->release(dev);
	else if (dev->type && dev->type->release)
		dev->type->release(dev);
	else if (dev->class && dev->class->dev_release)
		dev->class->dev_release(dev);
	else
		WARN(1, KERN_ERR "Device '%s' does not have a release() function, it is broken and must be fixed. See Documentation/core-api/kobject.rst.\n",
			dev_name(dev));
	kfree(p);
}

static const void *device_namespace(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	const void *ns = NULL;

	if (dev->class && dev->class->ns_type)
		ns = dev->class->namespace(dev);

	return ns;
}

static void device_get_ownership(struct kobject *kobj, kuid_t *uid, kgid_t *gid)
{
	struct device *dev = kobj_to_dev(kobj);

	if (dev->class && dev->class->get_ownership)
		dev->class->get_ownership(dev, uid, gid);
}

static struct kobj_type device_ktype = {
	.release	= device_release,
	.sysfs_ops	= &dev_sysfs_ops,
	.namespace	= device_namespace,
	.get_ownership	= device_get_ownership,
};

static int dev_uevent_filter(struct kobject *kobj)
{
	const struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &device_ktype) {
		struct device *dev = kobj_to_dev(kobj);
		if (dev->bus)
			return 1;
		if (dev->class)
			return 1;
	}
	return 0;
}

static const char *dev_uevent_name(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);

	if (dev->bus)
		return dev->bus->name;
	if (dev->class)
		return dev->class->name;
	return NULL;
}

static int dev_uevent(struct kobject *kobj, struct kobj_uevent_env *env)
{
	/* Stub: uevent environment setup not needed for minimal kernel */
	return 0;
}

static const struct kset_uevent_ops device_uevent_ops = {
	.filter =	dev_uevent_filter,
	.name =		dev_uevent_name,
	.uevent =	dev_uevent,
};

static ssize_t uevent_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	/* Stub: sysfs uevent display not needed for minimal kernel */
	return 0;
}

/* Stub: uevent_store simplified for minimal kernel */
static ssize_t uevent_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR_RW(uevent);

/* Stub: online sysfs attributes simplified for minimal kernel */
static ssize_t online_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sysfs_emit(buf, "1\n");
}

static ssize_t online_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR_RW(online);

/* Stub: removable sysfs attribute simplified for minimal kernel */
static ssize_t removable_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "unknown\n");
}
static DEVICE_ATTR_RO(removable);

int device_add_groups(struct device *dev, const struct attribute_group **groups)
{
	return sysfs_create_groups(&dev->kobj, groups);
}

void device_remove_groups(struct device *dev,
			  const struct attribute_group **groups)
{
	sysfs_remove_groups(&dev->kobj, groups);
}

/* devm_device_add_group, devm_device_remove_group, devm_device_add_groups, devm_device_remove_groups removed - unused */

static void device_remove_attrs(struct device *dev)
{
	struct class *class = dev->class;
	const struct device_type *type = dev->type;

	if (dev->physical_location) {
		device_remove_group(dev, &dev_attr_physical_location_group);
		kfree(dev->physical_location);
	}

	device_remove_file(dev, &dev_attr_removable);
	device_remove_file(dev, &dev_attr_waiting_for_supplier);
	device_remove_file(dev, &dev_attr_online);
	device_remove_groups(dev, dev->groups);

	if (type)
		device_remove_groups(dev, type->groups);

	if (class)
		device_remove_groups(dev, class->dev_groups);
}

static ssize_t dev_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return print_dev_t(buf, dev->devt);
}
static DEVICE_ATTR_RO(dev);

struct kset *devices_kset;

/* devices_kset_move_last, device_create_file removed - no callers */

void device_remove_file(struct device *dev,
			const struct device_attribute *attr)
{
	if (dev)
		sysfs_remove_file(&dev->kobj, &attr->attr);
}

/* Removed: device_remove_file_self, device_create_bin_file, device_remove_bin_file - no callers */

static void klist_children_get(struct klist_node *n)
{
	struct device_private *p = to_device_private_parent(n);
	struct device *dev = p->device;

	get_device(dev);
}

static void klist_children_put(struct klist_node *n)
{
	struct device_private *p = to_device_private_parent(n);
	struct device *dev = p->device;

	put_device(dev);
}

void device_initialize(struct device *dev)
{
	dev->kobj.kset = devices_kset;
	kobject_init(&dev->kobj, &device_ktype);
	INIT_LIST_HEAD(&dev->dma_pools);
	mutex_init(&dev->mutex);
	lockdep_set_novalidate_class(&dev->mutex);
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_pm_init(dev);
	set_dev_node(dev, NUMA_NO_NODE);
	INIT_LIST_HEAD(&dev->links.consumers);
	INIT_LIST_HEAD(&dev->links.suppliers);
	INIT_LIST_HEAD(&dev->links.defer_sync);
	dev->links.status = DL_DEV_NO_DRIVER;
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	dev->dma_coherent = dma_default_coherent;
#endif
}

/* virtual_device_parent removed - unused */

/* class_dir struct and related functions removed - unused */

static DEFINE_MUTEX(gdp_mutex);

static inline bool live_in_glue_dir(struct kobject *kobj,
				    struct device *dev)
{
	if (!kobj || !dev->class ||
	    kobj->kset != &dev->class->p->glue_dirs)
		return false;
	return true;
}

static inline struct kobject *get_glue_dir(struct device *dev)
{
	return dev->kobj.parent;
}

static inline bool kobject_has_children(struct kobject *kobj)
{
	WARN_ON_ONCE(kref_read(&kobj->kref) == 0);

	return kobj->sd && kobj->sd->dir.subdirs;
}

static void cleanup_glue_dir(struct device *dev, struct kobject *glue_dir)
{
	unsigned int ref;

	
	if (!live_in_glue_dir(glue_dir, dev))
		return;

	mutex_lock(&gdp_mutex);
	
	ref = kref_read(&glue_dir->kref);
	if (!kobject_has_children(glue_dir) && !--ref)
		kobject_del(glue_dir);
	kobject_put(glue_dir);
	mutex_unlock(&gdp_mutex);
}

static void device_remove_class_symlinks(struct device *dev)
{
	/* Stub: sysfs class symlinks not needed for minimal kernel */
}

int dev_set_name(struct device *dev, const char *fmt, ...)
{
	va_list vargs;
	int err;

	va_start(vargs, fmt);
	err = kobject_set_name_vargs(&dev->kobj, fmt, vargs);
	va_end(vargs);
	return err;
}

static struct kobject *device_to_dev_kobj(struct device *dev)
{
	struct kobject *kobj;

	if (dev->class)
		kobj = dev->class->dev_kobj;
	else
		kobj = sysfs_dev_char_kobj;

	return kobj;
}

static void device_remove_sys_dev_entry(struct device *dev)
{
	struct kobject *kobj = device_to_dev_kobj(dev);
	char devt_str[15];

	if (kobj) {
		format_dev_t(devt_str, dev->devt);
		sysfs_remove_link(kobj, devt_str);
	}
}

static int device_private_init(struct device *dev)
{
	dev->p = kzalloc(sizeof(*dev->p), GFP_KERNEL);
	if (!dev->p)
		return -ENOMEM;
	dev->p->device = dev;
	klist_init(&dev->p->klist_children, klist_children_get,
		   klist_children_put);
	INIT_LIST_HEAD(&dev->p->deferred_probe);
	return 0;
}

int device_add(struct device *dev)
{
	struct device *parent;
	int error = -EINVAL;

	/* Minimal stub: simplified device registration */
	dev = get_device(dev);
	if (!dev)
		return error;

	if (!dev->p) {
		error = device_private_init(dev);
		if (error)
			goto done;
	}

	if (dev->init_name) {
		dev_set_name(dev, "%s", dev->init_name);
		dev->init_name = NULL;
	}

	if (!dev_name(dev) && dev->bus && dev->bus->dev_name)
		dev_set_name(dev, "%s%u", dev->bus->dev_name, dev->id);

	if (!dev_name(dev)) {
		error = -EINVAL;
		goto name_error;
	}

	parent = get_device(dev->parent);
	if (parent && (dev_to_node(dev) == NUMA_NO_NODE))
		set_dev_node(dev, dev_to_node(parent));

	error = kobject_add(&dev->kobj, dev->kobj.parent, NULL);
	if (error)
		goto parent_error;

	device_pm_add(dev);
	bus_probe_device(dev);

	error = 0;
	put_device(dev);
	return error;

parent_error:
	put_device(parent);
name_error:
	kfree(dev->p);
	dev->p = NULL;
done:
	put_device(dev);
	return error;
}

int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}

struct device *get_device(struct device *dev)
{
	return dev ? kobj_to_dev(kobject_get(&dev->kobj)) : NULL;
}

void put_device(struct device *dev)
{
	
	if (dev)
		kobject_put(&dev->kobj);
}

static bool kill_device(struct device *dev)
{
	
	device_lock_assert(dev);

	if (dev->p->dead)
		return false;
	dev->p->dead = true;
	return true;
}

void device_del(struct device *dev)
{
	struct device *parent = dev->parent;
	struct kobject *glue_dir = NULL;
	struct class_interface *class_intf;
	unsigned int noio_flag;

	device_lock(dev);
	kill_device(dev);
	device_unlock(dev);

	if (dev->fwnode && dev->fwnode->dev == dev)
		dev->fwnode->dev = NULL;

	
	noio_flag = memalloc_noio_save();
	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_DEL_DEVICE, dev);

	dpm_sysfs_remove(dev);
	if (parent)
		klist_del(&dev->p->knode_parent);
	if (MAJOR(dev->devt)) {
		devtmpfs_delete_node(dev);
		device_remove_sys_dev_entry(dev);
		device_remove_file(dev, &dev_attr_dev);
	}
	if (dev->class) {
		device_remove_class_symlinks(dev);

		mutex_lock(&dev->class->p->mutex);
		
		list_for_each_entry(class_intf,
				    &dev->class->p->interfaces, node)
			if (class_intf->remove_dev)
				class_intf->remove_dev(dev, class_intf);
		
		klist_del(&dev->p->knode_class);
		mutex_unlock(&dev->class->p->mutex);
	}
	device_remove_file(dev, &dev_attr_uevent);
	device_remove_attrs(dev);
	bus_remove_device(dev);
	device_pm_remove(dev);
	driver_deferred_probe_del(dev);
	device_platform_notify_remove(dev);
	device_links_purge(dev);

	if (dev->bus)
		blocking_notifier_call_chain(&dev->bus->p->bus_notifier,
					     BUS_NOTIFY_REMOVED_DEVICE, dev);
	kobject_uevent(&dev->kobj, KOBJ_REMOVE);
	glue_dir = get_glue_dir(dev);
	kobject_del(&dev->kobj);
	cleanup_glue_dir(dev, glue_dir);
	memalloc_noio_restore(noio_flag);
	put_device(parent);
}

void device_unregister(struct device *dev)
{
	device_del(dev);
	put_device(dev);
}

/* prev_device removed - unused */

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *p;

	if (n) {
		p = to_device_private_parent(n);
		dev = p->device;
	}
	return dev;
}

/* device_get_devnode removed - never called */

/* Stub: device_for_each_child not called externally */
int device_for_each_child(struct device *parent, void *data,
			  int (*fn)(struct device *dev, void *data))
{
	return 0;
}
/* device_for_each_child_reverse, device_find_child, device_find_child_by_name removed - never called */

int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", &device_uevent_ops, NULL);
	if (!devices_kset)
		return -ENOMEM;
	dev_kobj = kobject_create_and_add("dev", NULL);
	if (!dev_kobj)
		goto dev_kobj_err;
	sysfs_dev_block_kobj = kobject_create_and_add("block", dev_kobj);
	if (!sysfs_dev_block_kobj)
		goto block_kobj_err;
	sysfs_dev_char_kobj = kobject_create_and_add("char", dev_kobj);
	if (!sysfs_dev_char_kobj)
		goto char_kobj_err;

	return 0;

 char_kobj_err:
	kobject_put(sysfs_dev_block_kobj);
 block_kobj_err:
	kobject_put(dev_kobj);
 dev_kobj_err:
	kset_unregister(devices_kset);
	return -ENOMEM;
}

struct root_device {
	struct device dev;
	struct module *owner;
};

static inline struct root_device *to_root_device(struct device *d)
{
	return container_of(d, struct root_device, dev);
}

/* Removed: __root_device_register, root_device_unregister - no callers */

static void device_create_release(struct device *dev)
{
	kfree(dev);
}

static __printf(6, 0) struct device *
device_create_groups_vargs(struct class *class, struct device *parent,
			   dev_t devt, void *drvdata,
			   const struct attribute_group **groups,
			   const char *fmt, va_list args)
{
	struct device *dev = NULL;
	int retval = -ENODEV;

	if (class == NULL || IS_ERR(class))
		goto error;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	device_initialize(dev);
	dev->devt = devt;
	dev->class = class;
	dev->parent = parent;
	dev->groups = groups;
	dev->release = device_create_release;
	dev_set_drvdata(dev, drvdata);

	retval = kobject_set_name_vargs(&dev->kobj, fmt, args);
	if (retval)
		goto error;

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

struct device *device_create(struct class *class, struct device *parent,
			     dev_t devt, void *drvdata, const char *fmt, ...)
{
	va_list vargs;
	struct device *dev;

	va_start(vargs, fmt);
	dev = device_create_groups_vargs(class, parent, devt, drvdata, NULL,
					  fmt, vargs);
	va_end(vargs);
	return dev;
}

struct device *device_create_with_groups(struct class *class,
					 struct device *parent, dev_t devt,
					 void *drvdata,
					 const struct attribute_group **groups,
					 const char *fmt, ...)
{
	va_list vargs;
	struct device *dev;

	va_start(vargs, fmt);
	dev = device_create_groups_vargs(class, parent, devt, drvdata, groups,
					 fmt, vargs);
	va_end(vargs);
	return dev;
}

void device_destroy(struct class *class, dev_t devt)
{
	struct device *dev;

	dev = class_find_device_by_devt(class, devt);
	if (dev) {
		put_device(dev);
		device_unregister(dev);
	}
}

void device_shutdown(void)
{
	/* Stubbed: device shutdown not needed for minimal boot */
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	/* Stub: minimal error probe for tiny kernel */
	return err;
}

static inline bool fwnode_is_primary(struct fwnode_handle *fwnode)
{
	return fwnode && !IS_ERR(fwnode->secondary);
}

/* set_primary_fwnode, set_secondary_fwnode, device_set_of_node_from_dev, device_set_node removed - unused */

/* device_match_* functions removed - unused, except device_match_devt */
int device_match_devt(struct device *dev, const void *pdevt) { return 0; }
