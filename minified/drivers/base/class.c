
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

#define to_class_attr(_attr) container_of(_attr, struct class_attribute, attr)

static ssize_t class_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct subsys_private *cp = to_subsys_private(kobj);
	ssize_t ret = -EIO;

	if (class_attr->show)
		ret = class_attr->show(cp->class, class_attr, buf);
	return ret;
}

static ssize_t class_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct class_attribute *class_attr = to_class_attr(attr);
	struct subsys_private *cp = to_subsys_private(kobj);
	ssize_t ret = -EIO;

	if (class_attr->store)
		ret = class_attr->store(cp->class, class_attr, buf, count);
	return ret;
}

static void class_release(struct kobject *kobj)
{
	struct subsys_private *cp = to_subsys_private(kobj);
	struct class *class = cp->class;

	if (class->class_release)
		class->class_release(class);

	kfree(cp);
}

/* class_child_ns_type removed - child_ns_type never called */

static const struct sysfs_ops class_sysfs_ops = {
	.show = class_attr_show,
	.store = class_attr_store,
};

static struct kobj_type class_ktype = {
	.sysfs_ops = &class_sysfs_ops,
	.release = class_release,
	/* .child_ns_type removed - never called */
};

static struct kset *class_kset;

/* class_get, class_put removed - never called */

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

/* __class_register, class_unregister, class_create_release, __class_create removed - never called */

void class_dev_iter_init(struct class_dev_iter *iter, struct class *class,
			 struct device *start, const struct device_type *type)
{
	struct klist_node *start_knode = NULL;

	if (start)
		start_knode = &start->p->knode_class;
	klist_iter_init_node(&class->p->klist_devices, &iter->ki, start_knode);
	iter->type = type;
}

struct device *class_dev_iter_next(struct class_dev_iter *iter)
{
	struct klist_node *knode;
	struct device *dev;

	while (1) {
		knode = klist_next(&iter->ki);
		if (!knode)
			return NULL;
		dev = klist_class_to_dev(knode);
		if (!iter->type || iter->type == dev->type)
			return dev;
	}
}

void class_dev_iter_exit(struct class_dev_iter *iter)
{
	klist_iter_exit(&iter->ki);
}

/* class_for_each_device removed - never called */

struct device *class_find_device(struct class *class, struct device *start,
				 const void *data,
				 int (*match)(struct device *, const void *))
{
	struct class_dev_iter iter;
	struct device *dev;

	if (!class)
		return NULL;
	if (!class->p) {
		WARN(1, "%s called for class '%s' before it was initialized",
		     __func__, class->name);
		return NULL;
	}

	class_dev_iter_init(&iter, class, start, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		if (match(dev, data)) {
			get_device(dev);
			break;
		}
	}
	class_dev_iter_exit(&iter);

	return dev;
}

/* class_interface_register, class_interface_unregister, show_class_attr_string, class_compat_register,
   class_compat_unregister, class_compat_create_link, class_compat_remove_link removed - unused */

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL, NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}
