
#ifndef _SYSFS_H_
#define _SYSFS_H_

#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H
#include <linux/types.h>
#include <linux/atomic.h>
struct rb_node;
struct rb_root;
struct kernfs_elem_dir {
	unsigned long		subdirs;
	struct rb_root		*children;
	struct kernfs_root	*root;
	unsigned long		rev;
};
struct kernfs_node {
	atomic_t		count;
	atomic_t		active;
	struct kernfs_node	*parent;
	const char		*name;
	struct rb_node		*rb;
	const void		*ns;
	unsigned int		hash;
	struct kernfs_elem_dir	dir;
	void			*priv;
	u64			id;
	unsigned short		flags;
	umode_t			mode;
	void			*iattr;
};
#endif /* __LINUX_KERNFS_H */
#include <linux/compiler.h>
#include <linux/stat.h>
#include <linux/atomic.h>

struct kobject;
struct bin_attribute;

struct attribute {
	const char		*name;
	umode_t			mode;
};

struct attribute_group {
	const char		*name;
	umode_t			(*is_visible)(struct kobject *,
					      struct attribute *, int);
	umode_t			(*is_bin_visible)(struct kobject *,
						  struct bin_attribute *, int);
	struct attribute	**attrs;
	struct bin_attribute	**bin_attrs;
};

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

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
};

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif  
