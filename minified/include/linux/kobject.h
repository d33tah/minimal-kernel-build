
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
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/uidgid.h>

struct kobject {
	const char		*name;
	struct kref		kref;
};

#endif
