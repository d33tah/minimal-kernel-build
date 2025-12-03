
#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/kernfs.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/kobject_ns.h>
#include <linux/stat.h>
#include <linux/atomic.h>

struct kobject;
struct module;
struct bin_attribute;
enum kobj_ns_type;

struct attribute {
	const char		*name;
	umode_t			mode;
};

#define sysfs_attr_init(attr) do {} while (0)

struct attribute_group {
	const char		*name;
	umode_t			(*is_visible)(struct kobject *,
					      struct attribute *, int);
	umode_t			(*is_bin_visible)(struct kobject *,
						  struct bin_attribute *, int);
	struct attribute	**attrs;
	struct bin_attribute	**bin_attrs;
};


#define SYSFS_PREALLOC 010000

#define __ATTR(_name, _mode, _show, _store) {				\
	.attr = {.name = __stringify(_name),				\
		 .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _show,						\
	.store	= _store,						\
}

#define __ATTR_PREALLOC(_name, _mode, _show, _store) {			\
	.attr = {.name = __stringify(_name),				\
		 .mode = SYSFS_PREALLOC | VERIFY_OCTAL_PERMISSIONS(_mode) },\
	.show	= _show,						\
	.store	= _store,						\
}

#define __ATTR_RO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = 0444 },		\
	.show	= _name##_show,						\
}

#define __ATTR_RO_MODE(_name, _mode) {					\
	.attr	= { .name = __stringify(_name),				\
		    .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _name##_show,						\
}

#define __ATTR_RW_MODE(_name, _mode) {					\
	.attr	= { .name = __stringify(_name),				\
		    .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _name##_show,						\
	.store	= _name##_store,					\
}

#define __ATTR_WO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = 0200 },		\
	.store	= _name##_store,					\
}

#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)

#define __ATTR_NULL { .attr = { .name = NULL } }

#define __ATTR_IGNORE_LOCKDEP	__ATTR

#define __ATTRIBUTE_GROUPS(_name)				\
static const struct attribute_group *_name##_groups[] = {	\
	&_name##_group,						\
	NULL,							\
}

#define ATTRIBUTE_GROUPS(_name)					\
static const struct attribute_group _name##_group = {		\
	.attrs = _name##_attrs,					\
};								\
__ATTRIBUTE_GROUPS(_name)

/* BIN_ATTRIBUTE_GROUPS removed - unused */

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

#define sysfs_bin_attr_init(bin_attr) sysfs_attr_init(&(bin_attr)->attr)
/* __BIN_ATTR_*, BIN_ATTR_* macros removed - unused */

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
};


static inline int sysfs_create_dir_ns(struct kobject *kobj, const void *ns)
{
	return 0;
}

static inline void sysfs_remove_dir(struct kobject *kobj)
{
}

static inline int sysfs_create_file_ns(struct kobject *kobj,
				       const struct attribute *attr,
				       const void *ns)
{
	return 0;
}

static inline void sysfs_remove_file_ns(struct kobject *kobj,
					const struct attribute *attr,
					const void *ns)
{
}

static inline int sysfs_create_bin_file(struct kobject *kobj,
					const struct bin_attribute *attr)
{
	return 0;
}

static inline void sysfs_remove_bin_file(struct kobject *kobj,
					 const struct bin_attribute *attr)
{
}

static inline int sysfs_create_link(struct kobject *kobj,
				    struct kobject *target, const char *name)
{
	return 0;
}

static inline void sysfs_remove_link(struct kobject *kobj, const char *name)
{
}

static inline int sysfs_create_group(struct kobject *kobj,
				     const struct attribute_group *grp)
{
	return 0;
}

static inline int sysfs_create_groups(struct kobject *kobj,
				      const struct attribute_group **groups)
{
	return 0;
}

static inline void sysfs_remove_group(struct kobject *kobj,
				      const struct attribute_group *grp)
{
}

static inline void sysfs_remove_groups(struct kobject *kobj,
				       const struct attribute_group **groups)
{
}

static inline void sysfs_notify(struct kobject *kobj, const char *dir,
				const char *attr)
{
}

static inline int __must_check sysfs_init(void)
{
	return 0;
}



__printf(2, 3)
static inline int sysfs_emit(char *buf, const char *fmt, ...)
{
	return 0;
}

static inline int __must_check sysfs_create_file(struct kobject *kobj,
						 const struct attribute *attr)
{
	return sysfs_create_file_ns(kobj, attr, NULL);
}

static inline void sysfs_remove_file(struct kobject *kobj,
				     const struct attribute *attr)
{
	sysfs_remove_file_ns(kobj, attr, NULL);
}

static inline void sysfs_put(struct kernfs_node *kn)
{
	kernfs_put(kn);
}

#endif  
