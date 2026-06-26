
#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/stat.h>
#include <linux/atomic.h>

struct kobject;
struct module;

/* Removed (0 users tree-wide): struct attribute, struct attribute_group, struct bin_attribute,
   the __ATTR* macros, ATTRIBUTE_GROUPS/__ATTRIBUTE_GROUPS,
   sysfs_attr_init/sysfs_bin_attr_init, SYSFS_PREALLOC. */



static inline int __must_check sysfs_init(void)
{
	return 0;
}





#endif  
