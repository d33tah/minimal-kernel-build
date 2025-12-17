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

enum oom_constraint {
	CONSTRAINT_NONE,
	CONSTRAINT_CPUSET,
	CONSTRAINT_MEMORY_POLICY,
	CONSTRAINT_MEMCG,
};

struct oom_control {
	 
	struct zonelist *zonelist;

	 
	nodemask_t *nodemask;

	 
	struct mem_cgroup *memcg;

	 
	const gfp_t gfp_mask;

	 
	const int order;

	 
	unsigned long totalpages;
	struct task_struct *chosen;
	long chosen_points;

	 
	enum oom_constraint constraint;
};

/* oom_lock, oom_adj_mutex removed - never used */


static inline bool tsk_is_oom_victim(struct task_struct * tsk)
{
	return tsk->signal->oom_mm;
}

static inline bool mm_is_oom_victim(struct mm_struct *mm)
{
	return test_bit(MMF_OOM_VICTIM, &mm->flags);
}

static inline vm_fault_t check_stable_address_space(struct mm_struct *mm)
{
	if (unlikely(test_bit(MMF_UNSTABLE, &mm->flags)))
		return VM_FAULT_SIGBUS;
	return 0;
}

bool __oom_reap_task_mm(struct mm_struct *mm);

/* oom_badness removed - unused */

extern bool out_of_memory(struct oom_control *oc);

extern void exit_oom_victim(void);

/* register_oom_notifier, unregister_oom_notifier removed - never called */

/* extern void oom_killer_enable(void); removed - never called */

/* find_lock_task_mm removed - unused */

#endif  
