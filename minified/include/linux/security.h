#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/mm.h>

struct mm_struct;

extern unsigned long mmap_min_addr;

static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);
}

#endif
