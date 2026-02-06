
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
/* linux/module.h removed - no module features used */
#include <linux/slab.h>
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
#include <linux/backing-dev.h>

#include "base.h"
#include "power/power.h"

/* Merged from init.c */
void __init driver_init(void)
{
	bdi_init(&noop_backing_dev_info);
	devices_init();
	buses_init();
	classes_init();
}

/* Removed: fwnode_link_add, fwnode_links_purge, fw_devlink_purge_absent_suppliers - no callers */

/* bool dma_default_coherent removed - never used */
/* device_links_srcu removed - never used */

/* device_pm_move_to_tail removed - was empty stub (lock/unlock with no work) */

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

/* device_namespace, device_get_ownership removed - callbacks never invoked */

static struct kobj_type device_ktype = {
	.release = device_release,
	.sysfs_ops = &dev_sysfs_ops,
	/* .namespace, .get_ownership removed - never invoked */
};

/* dev_uevent_filter, dev_uevent_name, dev_uevent, device_uevent_ops removed - never invoked */

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
	/* INIT_LIST_HEAD(&dev->dma_pools) removed - field removed */
	mutex_init(&dev->mutex);
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_pm_init(dev);
	set_dev_node(dev, NUMA_NO_NODE);
	INIT_LIST_HEAD(&dev->links.consumers);
	INIT_LIST_HEAD(&dev->links.suppliers);
	INIT_LIST_HEAD(&dev->links.defer_sync);
	dev->links.status = DL_DEV_NO_DRIVER;
	/* DMA coherent init removed - CONFIG_ARCH_HAS_SYNC_DMA_* not defined */
}

static DEFINE_MUTEX(gdp_mutex);

/* device_remove_class_symlinks was empty stub - removed */

int dev_set_name(struct device *dev, const char *fmt, ...)
{
	va_list vargs;
	int err;

	va_start(vargs, fmt);
	err = kobject_set_name_vargs(&dev->kobj, fmt, vargs);
	va_end(vargs);
	return err;
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
		dev->p = kzalloc(sizeof(*dev->p), GFP_KERNEL);
		if (!dev->p) {
			error = -ENOMEM;
			goto done;
		}
		dev->p->device = dev;
		klist_init(&dev->p->klist_children, klist_children_get,
			   klist_children_put);
		INIT_LIST_HEAD(&dev->p->deferred_probe);
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

/* device_register removed - no callers */

struct device *get_device(struct device *dev)
{
	return dev ? kobj_to_dev(kobject_get(&dev->kobj)) : NULL;
}

void put_device(struct device *dev)
{
	if (dev)
		kobject_put(&dev->kobj);
}

/* kill_device inlined - sets dev->p->dead = true */

void device_del(struct device *dev)
{
	struct device *parent = dev->parent;
	struct kobject *glue_dir = NULL;
	unsigned int noio_flag;

	device_lock(dev);
	dev->p->dead = true;
	device_unlock(dev);

	if (dev->fwnode && dev->fwnode->dev == dev)
		dev->fwnode->dev = NULL;

	noio_flag = memalloc_noio_save();

	if (parent)
		klist_del(&dev->p->knode_parent);
	/* MAJOR(dev->devt) check removed - device_remove_file is empty stub */
	if (dev->class) {
		/* device_remove_class_symlinks was empty stub */
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
	glue_dir = dev->kobj.parent; /* Inlined get_glue_dir */
	kobject_del(&dev->kobj);

	/* Inlined cleanup_glue_dir */
	if (glue_dir && dev->class &&
	    glue_dir->kset == &dev->class->p->glue_dirs) {
		unsigned int ref;

		mutex_lock(&gdp_mutex);
		ref = kref_read(&glue_dir->kref);
		WARN_ON_ONCE(kref_read(&glue_dir->kref) == 0);
		if (!(glue_dir->sd && glue_dir->sd->dir.subdirs) && !--ref)
			kobject_del(glue_dir);
		kobject_put(glue_dir);
		mutex_unlock(&gdp_mutex);
	}

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
	devices_kset = kset_create_and_add("devices", NULL);
	return 0;
}

/* Removed: root_device struct, __root_device_register, root_device_unregister - no callers */

/* device_create_release, device_create_groups_vargs, device_create,
   device_destroy removed - no callers */

/* dev_err_probe removed - never called */

int device_match_devt(struct device *dev, const void *pdevt)
{
	return 0;
}
