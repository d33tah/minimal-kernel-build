
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
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)
#include <asm/orc_types.h>

struct module;
struct exception_table_entry;


extern int init_module(void);
extern void cleanup_module(void);

/* CONFIG_MODULES off: built-in-only, MODULE never defined. */
#define module_init(x)	__initcall(x);


#define __init_or_module __init

#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

#define MODULE_FILE	MODULE_INFO(file, KBUILD_MODFILE);

#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)

struct notifier_block;

#endif
