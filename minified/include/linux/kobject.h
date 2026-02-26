
#ifndef _KOBJECT_H_
#define _KOBJECT_H_

/* sysfs.h inlined */
#include <linux/stat.h>
struct attribute {
	const char		*name;
	umode_t			mode;
};
struct attribute_group {
	struct attribute	**attrs;
};
static inline int __must_check sysfs_init(void) { return 0; }
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/uidgid.h>

struct kobject {
	const char		*name;
	const struct kobj_type	*ktype;
	struct kref		kref;
};


struct kobj_type {
};

struct kset {
	struct kobject kobj;
};

#endif
