
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/of.h>
/* of_device.h removed - only has includes, no definitions used */
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sysfs.h>
#include <linux/dma-map-ops.h>

#include "base.h"
#include "power/power.h"

/* Removed: fwnode_link_add, fwnode_links_purge, fw_devlink_purge_absent_suppliers - no callers */

DEFINE_STATIC_SRCU(device_links_srcu);

void device_pm_move_to_tail(struct device *dev)
{
	int idx;

	idx = srcu_read_lock(&device_links_srcu);
	srcu_read_unlock(&device_links_srcu, idx);
}

/* devlink_class and related functions removed - devlink_class_init was removed so class is never registered */

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
	.show = dev_attr_show,
	.store = dev_attr_store,
};

static void device_release(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	struct device_private *p = dev->p;

	kfree(dev->dma_range_map);

	if (dev->release)
		dev->release(dev);
	else if (dev->type && dev->type->release)
		dev->type->release(dev);
	else if (dev->class && dev->class->dev_release)
		dev->class->dev_release(dev);
	else
		WARN(1,
		     KERN_ERR
		     "Device '%s' does not have a release() function, it is broken and must be fixed. See Documentation/core-api/kobject.rst.\n",
		     dev_name(dev));
	kfree(p);
}

static const void *device_namespace(struct kobject *kobj)
{
	/* ns_type is never set, so namespace() is never called */
	return NULL;
}

static void device_get_ownership(struct kobject *kobj, kuid_t *uid, kgid_t *gid)
{
	/* class->get_ownership is never set */
}

static struct kobj_type device_ktype = {
	.release = device_release,
	.sysfs_ops = &dev_sysfs_ops,
	.namespace = device_namespace,
	.get_ownership = device_get_ownership,
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
	.filter = dev_uevent_filter,
	.name = dev_uevent_name,
	.uevent = dev_uevent,
};

/* uevent_show, uevent_store, dev_attr_uevent removed - only caller was empty device_remove_file stub */

/* Stub: online sysfs attributes simplified for minimal kernel */

/* device_add_groups, device_remove_groups removed - calls removed, functions did nothing */

/* device_remove_attrs removed - physical_location field removed, now empty */

/* device_remove_file_self, device_create_bin_file, device_remove_bin_file - no callers */

struct kset *devices_kset;

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
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_pm_init(dev);
	set_dev_node(dev, NUMA_NO_NODE);
	INIT_LIST_HEAD(&dev->links.consumers);
	INIT_LIST_HEAD(&dev->links.suppliers);
	INIT_LIST_HEAD(&dev->links.defer_sync);
	dev->links.status = DL_DEV_NO_DRIVER;
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) ||  \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	dev->dma_coherent = dma_default_coherent;
#endif
}

static DEFINE_MUTEX(gdp_mutex);

static inline bool live_in_glue_dir(struct kobject *kobj, struct device *dev)
{
	if (!kobj || !dev->class || kobj->kset != &dev->class->p->glue_dirs)
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
	if (dev->p->dead)
		return false;
	dev->p->dead = true;
	return true;
}

void device_del(struct device *dev)
{
	struct device *parent = dev->parent;
	struct kobject *glue_dir = NULL;
	unsigned int noio_flag;

	device_lock(dev);
	kill_device(dev);
	device_unlock(dev);

	if (dev->fwnode && dev->fwnode->dev == dev)
		dev->fwnode->dev = NULL;

	noio_flag = memalloc_noio_save();

	if (parent)
		klist_del(&dev->p->knode_parent);
	/* MAJOR(dev->devt) check removed - device_remove_file is empty stub */
	if (dev->class) {
		device_remove_class_symlinks(dev);

		mutex_lock(&dev->class->p->mutex);
		/* class_intf loop removed - interfaces list never populated */
		klist_del(&dev->p->knode_class);
		mutex_unlock(&dev->class->p->mutex);
	}
	/* device_remove_attrs call removed - function now empty */
	bus_remove_device(dev);
	driver_deferred_probe_del(dev);
	/* device_platform_notify_remove, device_links_purge, bus_notifier calls removed */
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

/* Simplified - kobject_create_and_add returns NULL (stub), return ignored */
int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", &device_uevent_ops, NULL);
	return 0;
}

/* Removed: root_device struct, __root_device_register, root_device_unregister - no callers */

static void device_create_release(struct device *dev)
{
	kfree(dev);
}

static __printf(6, 0) struct device *device_create_groups_vargs(
	struct class *class, struct device *parent, dev_t devt, void *drvdata,
	const struct attribute_group **groups, const char *fmt, va_list args)
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
	/* dev->groups removed - field no longer exists (sysfs stubbed) */
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

/* device_create_with_groups removed - groups parameter now ignored (sysfs stubbed) */

void device_destroy(struct class *class, dev_t devt)
{
	struct device *dev;

	dev = class_find_device_by_devt(class, devt);
	if (dev) {
		put_device(dev);
		device_unregister(dev);
	}
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	/* Stub: minimal error probe for tiny kernel */
	return err;
}

int device_match_devt(struct device *dev, const void *pdevt)
{
	return 0;
}
