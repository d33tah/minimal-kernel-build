
#ifndef _DEVICE_CLASS_H_
#define _DEVICE_CLASS_H_

#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/pm.h>
#include <linux/device/bus.h>

struct device;
struct fwnode_handle;

struct class {
	const char		*name;
	struct module		*owner;

	/* class_groups, dev_groups, dev_kobj, dev_uevent, devnode, class_release, shutdown_pre removed */
	void (*dev_release)(struct device *dev);

	/* ns_type, namespace, get_ownership, pm removed - never set/accessed */

	struct subsys_private *p;
};

struct class_dev_iter {
	struct klist_iter		ki;
	const struct device_type	*type;
};

/* sysfs_dev_block_kobj, sysfs_dev_char_kobj removed - unused */
/* __class_register, class_register, class_unregister, struct class_compat,
   class_compat_register, class_compat_unregister, class_compat_create_link,
   class_compat_remove_link removed - never called */

extern void class_dev_iter_init(struct class_dev_iter *iter,
				struct class *class,
				struct device *start,
				const struct device_type *type);
extern struct device *class_dev_iter_next(struct class_dev_iter *iter);
extern void class_dev_iter_exit(struct class_dev_iter *iter);

/* class_for_each_device removed - never called */
extern struct device *class_find_device(struct class *class,
					struct device *start, const void *data,
					int (*match)(struct device *, const void *));

/* class_find_device_by_name, class_find_device_by_of_node,
 * class_find_device_by_fwnode, class_find_device_by_acpi_dev removed - unused */

static inline struct device *class_find_device_by_devt(struct class *class,
						       dev_t devt)
{
	return class_find_device(class, NULL, &devt, device_match_devt);
}

/* struct class_attribute removed - never used */
/* struct class_interface removed - never used */
/* class_interface_register, __class_create, class_create macro removed - never called */

#endif	 
