
/* class.h inlined into device.h */
#include <linux/device.h>
/* linux/module.h removed - no module features used */
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include "base.h"

/* class_attr_show, class_attr_store, class_release, class_sysfs_ops,
   class_ktype all removed - class_ktype never used */

static struct kset *class_kset;

/* class_dev_iter_init, class_dev_iter_next, class_dev_iter_exit,
   class_find_device removed - never called */

/* class_interface_register, class_interface_unregister, show_class_attr_string, class_compat_register,
   class_compat_unregister, class_compat_create_link, class_compat_remove_link removed - unused */

int __init classes_init(void)
{
	class_kset = kset_create_and_add("class", NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0;
}
