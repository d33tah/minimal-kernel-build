
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/stat.h>
#include <linux/cache.h>
#include <linux/elf.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>

#include <linux/srcu.h>
#include <linux/static_call_types.h>


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
