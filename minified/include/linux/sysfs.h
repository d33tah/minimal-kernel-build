
#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/kernfs.h>
#include <linux/compiler.h>
/* linux/errno.h removed - no errno constants used */
#include <linux/list.h>
#include <linux/lockdep.h>
/* kobject_ns.h removed - never used */
#include <linux/stat.h>
#include <linux/atomic.h>

struct kobject;
struct bin_attribute;
/* enum kobj_ns_type forward declaration removed - never used */

struct attribute {
	const char		*name;
	umode_t			mode;
};

/* sysfs_attr_init removed - unused */

struct attribute_group {
	const char		*name;
	umode_t			(*is_visible)(struct kobject *,
					      struct attribute *, int);
	umode_t			(*is_bin_visible)(struct kobject *,
						  struct bin_attribute *, int);
	struct attribute	**attrs;
	struct bin_attribute	**bin_attrs;
};


/* __ATTR, __ATTR_RO, __ATTR_WO, __ATTR_RW, __ATTRIBUTE_GROUPS, ATTRIBUTE_GROUPS removed - unused */

struct file;
struct vm_area_struct;
struct address_space;

struct bin_attribute {
	struct attribute	attr;
	size_t			size;
	void			*private;
	struct address_space *(*f_mapping)(void);
	ssize_t (*read)(struct file *, struct kobject *, struct bin_attribute *,
			char *, loff_t, size_t);
	ssize_t (*write)(struct file *, struct kobject *, struct bin_attribute *,
			 char *, loff_t, size_t);
	int (*mmap)(struct file *, struct kobject *, struct bin_attribute *attr,
		    struct vm_area_struct *vma);
};

/* sysfs_bin_attr_init removed - unused */

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
};

/* sysfs_notify removed - unused */

static inline int __must_check sysfs_init(void)
{
	return 0;
}



/* sysfs_emit removed - no callers */

#endif  
