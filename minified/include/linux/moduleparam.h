#ifndef _LINUX_MODULE_PARAMS_H
#define _LINUX_MODULE_PARAMS_H
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/kernel.h>

#define MODULE_PARAM_PREFIX KBUILD_MODNAME "."
#define __MODULE_INFO_PREFIX KBUILD_MODNAME "."


#define __MODULE_INFO(tag, name, info)					  	static const char __UNIQUE_ID(name)[]				  		__used __section(".modinfo") __aligned(1)		  		= __MODULE_INFO_PREFIX __stringify(tag) "=" info

struct kernel_param;

struct kernel_param_ops {
	 
	unsigned int flags;
	 
	int (*set)(const char *val, const struct kernel_param *kp);
};

struct kernel_param {
	const char *name;
	struct module *mod;
	const struct kernel_param_ops *ops;
	const u16 perm;
	s8 level;
	u8 flags;
	void *arg;
};

extern const struct kernel_param __start___param[], __stop___param[];


/* kernel_param_lock/kernel_param_unlock: 0-caller empty-body stubs removed */

/* parse_args: no-op cmdline stub removed (empty-cmdline honest boot) */


struct module;

#endif  
