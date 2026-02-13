/* Minimal security.h - stubs for !CONFIG_SECURITY */
#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>

extern unsigned long mmap_min_addr;

/* Only two security functions are actually called */
static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);  /* Stub: always assume capability present */
}

#endif
