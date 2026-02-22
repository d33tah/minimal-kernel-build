
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

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
};

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif
