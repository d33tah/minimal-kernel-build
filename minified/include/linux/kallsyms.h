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

/* is_kernel_text removed - never called */

static inline void *dereference_symbol_descriptor(void *ptr)
{
	return ptr;
}

#endif  
