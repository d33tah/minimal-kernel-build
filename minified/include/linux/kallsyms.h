#ifndef _LINUX_KALLSYMS_H
#define _LINUX_KALLSYMS_H

#include <linux/errno.h>
#define BUILD_ID_SIZE_MAX 20
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/sections.h>

#define KSYM_NAME_LEN 128
#define KSYM_SYMBOL_LEN (sizeof("%s+%#lx/%#lx [%s %s]") + \
			(KSYM_NAME_LEN - 1) + \
			2*(BITS_PER_LONG*3/10) + (MODULE_NAME_LEN - 1) + \
			(BUILD_ID_SIZE_MAX * 2) + 1)

struct cred;
struct module;

static inline int is_kernel_text(unsigned long addr)
{
	if (__is_kernel_text(addr))
		return 1;
	return in_gate_area_no_mm(addr);
}

static inline int is_kernel(unsigned long addr)
{
	if (__is_kernel(addr))
		return 1;
	return in_gate_area_no_mm(addr);
}


static inline void *dereference_symbol_descriptor(void *ptr)
{
	return ptr;
}

#endif  
