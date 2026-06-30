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


static inline void kernel_param_lock(struct module *mod)
{
}
static inline void kernel_param_unlock(struct module *mod)
{
}

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
