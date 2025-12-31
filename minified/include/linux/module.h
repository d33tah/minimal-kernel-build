
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/list.h>
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>

enum {
	EI_ETYPE_NONE,
	EI_ETYPE_NULL,
	EI_ETYPE_ERRNO,
	EI_ETYPE_ERRNO_NULL,
	EI_ETYPE_TRUE,
};
#define ALLOW_ERROR_INJECTION(fname, _etype)
/* end error-injection.h */
#include <linux/tracepoint-defs.h>
#include <linux/srcu.h>
#include <linux/static_call_types.h>


#include <linux/percpu.h>
/* --- 2025-12-07 20:50 --- Inlined asm/module.h */
#define Elf_Shdr	Elf32_Shdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Sym		Elf32_Sym
#define Elf_Dyn		Elf32_Dyn
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)
#include <asm/orc_types.h>
/* struct mod_arch_specific removed - unused */
#define MODULE_NAME_LEN MAX_PARAM_PREFIX_LEN

struct module;
struct exception_table_entry;

struct module_kobject {
	struct kobject kobj;
	struct module *mod;
	struct kobject *drivers_dir;
	struct module_param_attrs *mp;
	struct completion *kobj_completion;
} __randomize_layout;

struct module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct module_attribute *, struct module_kobject *,
			char *);
	ssize_t (*store)(struct module_attribute *, struct module_kobject *,
			 const char *, size_t count);
	void (*setup)(struct module *, const char *);
	int (*test)(struct module *);
	void (*free)(struct module *);
};

/* struct module_version_attribute removed - unused */

extern int init_module(void);

/* Built-in kernel, not building as module */
#define module_init(x)	__initcall(x);
#define module_exit(x)	__exitcall(x);

#define __init_or_module __init
#define __initdata_or_module __initdata
#define __initconst_or_module __initconst
#define __INIT_OR_MODULE __INIT
#define __INITDATA_OR_MODULE __INITDATA
#define __INITRODATA_OR_MODULE __INITRODATA

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* MODULE_ALIAS, MODULE_SOFTDEP, MODULE_AUTHOR, MODULE_DESCRIPTION, MODULE_VERSION, MODULE_FIRMWARE, MODULE_IMPORT_NS removed - unused */
#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);
#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)
#define MODULE_DEVICE_TABLE(type, name)

struct notifier_block;


/* __module_address, __module_text_address, is_module_address,
   is_module_percpu_address removed - unused (only is_module_text_address kept) */

static inline bool is_module_text_address(unsigned long addr)
{
	return false;
}

#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak,visibility("hidden"))); &(x); })
/* symbol_put removed - unused */

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

#define module_name(mod) "kernel"

#define module_put_and_kthread_exit(code) kthread_exit(code)


#define __MODULE_STRING(x) __stringify(x)

#endif  
