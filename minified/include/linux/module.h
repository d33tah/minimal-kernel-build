
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/list.h>
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>
#include <linux/tracepoint-defs.h>


#include <linux/percpu.h>
#include <asm/orc_types.h>

struct module;



/* CONFIG_MODULES off: built-in-only, MODULE never defined. */
#define module_init(x)	__initcall(x);


#define __init_or_module __init

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);

#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)


#endif
