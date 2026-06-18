
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
#include "power/power.h"


/* Removed: fwnode_link_add, fwnode_links_purge, fw_devlink_purge_absent_suppliers - no callers */

/* Removed: devlink class infrastructure (device_links_read_lock/unlock,
 * device_link_synchronize_removal, device_reorder_to_tail, to_devlink,
 * devlink_attrs/groups, device_link_release_fn, devlink_dev_release,
 * devlink_class, devlink_add/remove_symlinks, devlink_class_intf,
 * devlink_class_init, DL_*_FLAGS / FW_DEVLINK_FLAGS_*) - no device_link is ever
 * created (device_link_add has no callers), so the whole devlink class is dead.
 */

static struct kobject *dev_kobj;
struct kobject *sysfs_dev_char_kobj;
struct kobject *sysfs_dev_block_kobj;


static void device_release(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	struct device_private *p = dev->p;

	kfree(dev->dma_range_map);

	if (dev->release)
		dev->release(dev);
	else if (dev->type && dev->type->release)
		dev->type->release(dev);
	else
		WARN(1, KERN_ERR "Device '%s' does not have a release() function, it is broken and must be fixed. See Documentation/core-api/kobject.rst.\n",
			dev_name(dev));
	kfree(p);
}

static struct kobj_type device_ktype = {
	.release	= device_release,
};

/* Removed: dev_uevent_filter / dev_uevent_name / dev_uevent / device_uevent_ops
 * - the kset_uevent_ops table was stored in devices_kset but never dispatched:
 * kobject_uevent_env (lib/kobject_uevent.c) is a stub that never derefs uevent_ops.
 * Removed: uevent_show / uevent_store / dev_attr_uevent - the uevent sysfs attr
 * was only fed to the device_remove_file no-op stub, never created/read. */

/* Stub: online sysfs attributes simplified for minimal kernel */

/* Removed: dev_show / dev_attr_dev - the "dev" sysfs attr was only fed to the
 * device_remove_file no-op stub, never created or read. */

struct kset *devices_kset;


/* Removed: device_remove_file (0-caller no-op stub after dev_attr_dev/uevent
 * removal), device_remove_file_self, device_create_bin_file, device_remove_bin_file. */

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
}



/* Removed: gdp_mutex, live_in_glue_dir, get_glue_dir, kobject_has_children,
   cleanup_glue_dir - class glue_dirs are never populated (SYSFS=n; dev->kobj.kset
   is unconditionally devices_kset, never &class->p->glue_dirs), so
   live_in_glue_dir always returned false and cleanup_glue_dir early-returned. */

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

/* Removed: kill_device / device_del / device_unregister - the device teardown
   path is runtime-dead (no device is ever unregistered on this build); its only
   callers were the (now removed) tty teardown chain and device_destroy. */

int __init devices_init(void)
{
	devices_kset = kset_create_and_add("devices", NULL);
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
	/* kset_unregister(devices_kset) removed - boot never hits this path */
	return -ENOMEM;
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

int device_match_devt(struct device *dev, const void *pdevt) { return 0; }

/* Removed: device_destroy + driver_deferred_probe_del - device_destroy was only
   reached from the (removed) tty teardown path; driver_deferred_probe_del was
   only called from the (removed) device_del. */
