
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/stat.h>
#include <linux/cache.h>
#include <linux/elf.h>
#include <linux/kobject.h>
#include <linux/jump_label.h>

#include <linux/srcu.h>
#include <linux/stringify.h>

/* moduleparam.h inlined */
#define __MODULE_INFO_PREFIX KBUILD_MODNAME "."
#define __MODULE_INFO(tag, name, info)					  \
	static const char __UNIQUE_ID(name)[]				  \
		__used __section(".modinfo") __aligned(1)		  \
		= __MODULE_INFO_PREFIX __stringify(tag) "=" info

struct kernel_param;
enum { KERNEL_PARAM_OPS_FL_NOARG = (1 << 0) };
struct kernel_param_ops {
	unsigned int flags;
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
	void (*free)(void *arg);
};
enum { KERNEL_PARAM_FL_UNSAFE = (1 << 0), KERNEL_PARAM_FL_HWPARAM = (1 << 1) };
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
extern char *parse_args(const char *name, char *args,
		const struct kernel_param *params, unsigned num,
		s16 level_min, s16 level_max, void *arg,
		int (*unknown)(char *param, char *val,
			const char *doing, void *arg));

struct module;

#define __init_or_module __init

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);
#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)

static inline void __module_get(struct module *module)
{
}

static inline bool try_module_get(struct module *module)
{
	return true;
}

static inline void module_put(struct module *module)
{
}

#endif  
