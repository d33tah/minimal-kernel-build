
#ifndef _DEVICE_CLASS_H_
#define _DEVICE_CLASS_H_

#include <linux/kobject.h>

struct device;

struct class {
	const char		*name;

	/* dev_kobj field removed - write-only (assigned in __class_register
	   from sysfs_dev_char_kobj, never read by any consumer). */

	/* p (struct subsys_private *) removed - write-only (assigned in
	   __class_register, never read; the subsys_private is recovered via
	   to_subsys_private(kobj) container_of in class_release instead). */
};

/* sysfs_dev_block_kobj / sysfs_dev_char_kobj externs removed - both globals
   are only referenced inside drivers/base/core.c (sysfs anchor creation);
   no external consumer reads them. */
extern int __must_check __class_register(struct class *class,
					 struct lock_class_key *key);

/* class_register macro + class_unregister removed - sole user was the
   devlink class registration, which has been removed. */

/* struct class_compat, class_compat_register, class_compat_unregister,
   class_compat_create_link, class_compat_remove_link removed - unused */

/* class_find_device + class_find_device_by_devt/by_name/by_of_node/by_fwnode/
 * by_acpi_dev removed - sole caller was the now-removed tty_get_device chain. */

/* Removed: struct class_attribute + CLASS_ATTR_{RW,RO,WO} + struct
   class_attribute_string + CLASS_ATTR_STRING + struct class_interface - no
   class attributes or class interfaces are defined anywhere, and the
   show_class_attr_string / class_interface_register users were already removed. */

extern struct class * __must_check __class_create(struct module *owner,
						  const char *name,
						  struct lock_class_key *key);

#define class_create(owner, name)		\
({						\
	static struct lock_class_key __key;	\
	__class_create(owner, name, &__key);	\
})


#endif	 
