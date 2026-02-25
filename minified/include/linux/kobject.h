
#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/sysfs.h>
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/uidgid.h>

struct kobject {
	const char		*name;
	const struct kobj_type	*ktype;
	struct kref		kref;
};

extern struct kobject * __must_check kobject_get_unless_zero(
						struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

struct kobj_type {
};

struct kset {
	struct kobject kobj;
};

#endif
