#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H

#include <linux/sched/signal.h>



static inline bool task_cputime(struct task_struct *t,
				u64 *utime, u64 *stime)
{
	*utime = t->utime;
	*stime = t->stime;
	return false;
}

static inline u64 task_gtime(struct task_struct *t)
{
	return t->gtime;
}



/*
 * Per-thread-group CPU-time accounting is driven only by POSIX CPU timers,
 * which are not present on this kernel (no signal_struct cputimer field, no
 * run_posix_cpu_timers). The group accounting therefore has no destination
 * and these hooks are no-ops.
 */
static inline void account_group_user_time(struct task_struct *tsk,
					   u64 cputime)
{
}

static inline void account_group_system_time(struct task_struct *tsk,
					     u64 cputime)
{
}

static inline void account_group_exec_runtime(struct task_struct *tsk,
					      unsigned long long ns)
{
}

static inline void prev_cputime_init(struct prev_cputime *prev)
{
	prev->utime = prev->stime = 0;
	raw_spin_lock_init(&prev->lock);
}


#endif  
