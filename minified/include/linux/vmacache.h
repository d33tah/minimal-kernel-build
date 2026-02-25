#ifndef __LINUX_VMACACHE_H
#define __LINUX_VMACACHE_H

#include <linux/sched.h>
#include <linux/mm.h>

static inline void vmacache_flush(struct task_struct *tsk)
{
	memset(tsk->vmacache.vmas, 0, sizeof(tsk->vmacache.vmas));
}

static inline void vmacache_update(unsigned long addr,
				   struct vm_area_struct *newvma) { }
static inline struct vm_area_struct *vmacache_find(struct mm_struct *mm,
						   unsigned long addr)
{
	return NULL;
}

#endif
