#ifndef _LINUX_KALLSYMS_H
#define _LINUX_KALLSYMS_H

#include <linux/errno.h>
/* BUILD_ID_SIZE_MAX removed - unused */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/sections.h>

/* KSYM_NAME_LEN removed - unused */

struct cred;
struct module;

static inline int is_kernel_text(unsigned long addr)
{
	if (__is_kernel_text(addr))
		return 1;
	return in_gate_area_no_mm(addr);
}

static inline void *dereference_symbol_descriptor(void *ptr)
{
	return ptr;
}

#endif  
