
#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/atomic.h>
#include <linux/stat.h>

struct kobject;

struct attribute {
	const char		*name;
	umode_t			mode;
};

struct attribute_group {
	struct attribute	**attrs;
};

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif
