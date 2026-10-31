
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/list.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/moduleparam.h>
#include <linux/tracepoint-defs.h>

#include <asm/orc_types.h>

/* CONFIG_MODULES off: built-in-only, MODULE never defined. */
#define module_init(x) __initcall(x);

#define __init_or_module __init

#endif
