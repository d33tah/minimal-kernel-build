
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>

#include <linux/srcu.h>
#include <linux/static_call_types.h>

#define Elf_Shdr	Elf32_Shdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Sym		Elf32_Sym
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)

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
