#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H

#include <linux/sched/signal.h>



/*
 * Per-thread-group CPU-time accounting is driven only by POSIX CPU timers,
 * which are not present on this kernel (no signal_struct cputimer field, no
 * run_posix_cpu_timers). The group accounting therefore has no destination
 * and these hooks are no-ops.
 */
static inline void account_group_exec_runtime(struct task_struct *tsk,
					      unsigned long long ns)
{
}


#endif
