
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

/* linux/list.h removed - no list structures used */
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
/* kmod.h removed - was just includes, content removed */
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>

/* error-injection.h macros removed - unused */
#include <linux/tracepoint-defs.h>
#include <linux/srcu.h>
#include <linux/static_call_types.h>


/* --- 2025-12-07 20:50 --- Inlined asm/module.h */
#define Elf_Shdr	Elf32_Shdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Sym		Elf32_Sym
/* Elf_Dyn removed - unused */
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)
/* struct mod_arch_specific, MODULE_NAME_LEN removed - unused */

struct module;
/* struct exception_table_entry forward decl removed - unused */
/* struct module_kobject, module_attribute removed - never used */

/* struct module_version_attribute removed - unused */
/* extern int init_module(void) removed - never called */
/* module_init, module_exit removed - never used (no modules) */

#define __init_or_module __init
/* __initdata_or_module, __initconst_or_module removed - unused */

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* MODULE_ALIAS, MODULE_SOFTDEP, MODULE_AUTHOR, MODULE_DESCRIPTION, MODULE_VERSION, MODULE_FIRMWARE, MODULE_IMPORT_NS removed - unused */
#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);
#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)
/* MODULE_DEVICE_TABLE removed - never called */
/* struct notifier_block forward decl removed - unused */

/* __module_address, __module_text_address, is_module_address,
   is_module_percpu_address, is_module_text_address removed - unused */

/* symbol_get, symbol_put removed - unused */

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

/* module_name, module_put_and_kthread_exit, __MODULE_STRING removed - unused */

#endif  
