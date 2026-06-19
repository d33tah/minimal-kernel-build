
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

	kfree(cp);
}

static struct kobj_type class_ktype = {
	.release	= class_release,
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

	retval = __class_register(cls, key);
	if (retval)
		goto error;

	return cls;

error:
	kfree(cls);
	return ERR_PTR(retval);
}


/* Removed: class_dev_iter_init/next/exit and class_find_device - the
   device-iteration helpers and class_find_device (sole caller was the now-removed
   tty_get_device chain) are all dead. */

/* Removed: class_interface_register - sole caller was the devlink class
 * registration, which has been removed. class_interface_unregister,
 * show_class_attr_string, class_compat_* were already removed - unused. */

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}


