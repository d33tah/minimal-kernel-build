#ifndef _LINUX_MODULE_PARAMS_H
#define _LINUX_MODULE_PARAMS_H
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/kernel.h>

struct kernel_param;

struct kernel_param {
	const char *name;
};

extern const struct kernel_param __start___param[], __stop___param[];

/* kernel_param_lock/kernel_param_unlock: 0-caller empty-body stubs removed */

/* parse_args: no-op cmdline stub removed (empty-cmdline honest boot) */

struct module;

#endif
