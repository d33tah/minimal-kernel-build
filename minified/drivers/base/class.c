
#include <linux/device/class.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include "base.h"

static void class_release(struct kobject *kobj)
{
	struct subsys_private *cp = to_subsys_private(kobj);
	struct class *class = cp->class;

	if (class->class_release)
		class->class_release(class);

	kfree(cp);
}

static const struct kobj_ns_type_operations *class_child_ns_type(struct kobject *kobj)
{
	struct subsys_private *cp = to_subsys_private(kobj);
	struct class *class = cp->class;

	return class->ns_type;
}

static struct kobj_type class_ktype = {
	.release	= class_release,
	.child_ns_type	= class_child_ns_type,
};

static struct kset *class_kset;



static struct class *class_get(struct class *cls)
{
	if (cls)
		kset_get(&cls->p->subsys);
	return cls;
}

static void class_put(struct class *cls)
{
	if (cls)
		kset_put(&cls->p->subsys);
}

static struct device *klist_class_to_dev(struct klist_node *n)
{
	struct device_private *p = to_device_private_class(n);
	return p->device;
}

static void klist_class_dev_get(struct klist_node *n)
{
	struct device *dev = klist_class_to_dev(n);

	get_device(dev);
}

static void klist_class_dev_put(struct klist_node *n)
{
	struct device *dev = klist_class_to_dev(n);

	put_device(dev);
}

int __class_register(struct class *cls, struct lock_class_key *key)
{
	struct subsys_private *cp;
	int error;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;
	klist_init(&cp->klist_devices, klist_class_dev_get, klist_class_dev_put);
	kset_init(&cp->glue_dirs);
	__mutex_init(&cp->mutex, "subsys mutex", key);
	error = kobject_set_name(&cp->subsys.kobj, "%s", cls->name);
	if (error) {
		kfree(cp);
		return error;
	}

	 
	if (!cls->dev_kobj)
		cls->dev_kobj = sysfs_dev_char_kobj;

	cp->subsys.kobj.kset = class_kset;
	cp->subsys.kobj.ktype = &class_ktype;
	cp->class = cls;
	cls->p = cp;

	error = kset_register(&cp->subsys);
	if (error) {
		kfree(cp);
		return error;
	}
	class_get(cls);
	class_put(cls);
	return 0;
}

static void class_create_release(struct class *cls)
{
	kfree(cls);
}

struct class *__class_create(struct module *owner, const char *name,
			     struct lock_class_key *key)
{
	struct class *cls;
	int retval;

	cls = kzalloc(sizeof(*cls), GFP_KERNEL);
	if (!cls) {
		retval = -ENOMEM;
		goto error;
	}

	cls->name = name;
	cls->owner = owner;
	cls->class_release = class_create_release;

	retval = __class_register(cls, key);
	if (retval)
		goto error;

	return cls;

error:
	kfree(cls);
	return ERR_PTR(retval);
}


/* Removed: class_dev_iter_init/next/exit - the device-iteration helpers were
   only ever called from class_find_device below (no external callers), so they
   are folded inline here. */

struct device *class_find_device(struct class *class, struct device *start,
				 const void *data,
				 int (*match)(struct device *, const void *))
{
	struct klist_iter ki;
	struct klist_node *start_knode = NULL;
	struct klist_node *knode;
	struct device *dev;

	if (!class)
		return NULL;
	if (!class->p) {
		WARN(1, "%s called for class '%s' before it was initialized",
		     __func__, class->name);
		return NULL;
	}

	if (start)
		start_knode = &start->p->knode_class;
	klist_iter_init_node(&class->p->klist_devices, &ki, start_knode);
	while ((knode = klist_next(&ki))) {
		dev = klist_class_to_dev(knode);
		if (match(dev, data)) {
			get_device(dev);
			goto out;
		}
	}
	dev = NULL;
out:
	klist_iter_exit(&ki);

	return dev;
}

/* Removed: class_interface_register - sole caller was the devlink class
 * registration, which has been removed. class_interface_unregister,
 * show_class_attr_string, class_compat_* were already removed - unused. */

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL, NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}


