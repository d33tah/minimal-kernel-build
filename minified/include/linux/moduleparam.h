#ifndef _LINUX_MODULE_PARAMS_H
#define _LINUX_MODULE_PARAMS_H
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/kernel.h>

#define MODULE_PARAM_PREFIX KBUILD_MODNAME "."
#define __MODULE_INFO_PREFIX KBUILD_MODNAME "."

#define __MODULE_INFO(tag, name, info)					  \
	static const char __UNIQUE_ID(name)[]				  \
		__used __section(".modinfo") __aligned(1)		  \
		= __MODULE_INFO_PREFIX __stringify(tag) "=" info

struct kernel_param;

enum {
	KERNEL_PARAM_OPS_FL_NOARG = (1 << 0)
};

struct kernel_param_ops {
	 
	unsigned int flags;
	 
	int (*set)(const char *val, const struct kernel_param *kp);
	 
	int (*get)(char *buffer, const struct kernel_param *kp);
	 
	void (*free)(void *arg);
};

enum {
	KERNEL_PARAM_FL_UNSAFE	= (1 << 0),
	KERNEL_PARAM_FL_HWPARAM	= (1 << 1),
};

struct kernel_param {
	const char *name;
	struct module *mod;
	const struct kernel_param_ops *ops;
	const u16 perm;
	s8 level;
	u8 flags;
	union {
		void *arg;
		const struct kparam_string *str;
		const struct kparam_array *arr;
	};
};

extern const struct kernel_param __start___param[], __stop___param[];

extern bool parameq(const char *name1, const char *name2);

extern char *parse_args(const char *name,
		      char *args,
		      const struct kernel_param *params,
		      unsigned num,
		      s16 level_min,
		      s16 level_max,
		      void *arg,
		      int (*unknown)(char *param, char *val,
				     const char *doing, void *arg));

struct module;

#endif  
