/* Minimal oom.h */
#ifndef __INCLUDE_LINUX_OOM_H
#define __INCLUDE_LINUX_OOM_H

#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/nodemask.h>
#include <linux/sched/coredump.h>
#include <linux/mm.h>

struct zonelist;
struct notifier_block;
struct mem_cgroup;
struct task_struct;

static inline bool tsk_is_oom_victim(struct task_struct * tsk)
{
	return tsk->signal->oom_mm;
}

static inline vm_fault_t check_stable_address_space(struct mm_struct *mm)
{
	if (unlikely(test_bit(MMF_UNSTABLE, &mm->flags)))
		return VM_FAULT_SIGBUS;
	return 0;
}


#endif  
