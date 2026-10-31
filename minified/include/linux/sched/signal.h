#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/signal.h>
#include <linux/sched/task.h>

#include <linux/mm_types.h>

struct signal_struct {
	struct rlimit rlim[RLIM_NLIMITS];
	struct mutex cred_guard_mutex;
	struct rw_semaphore exec_update_lock;
} __randomize_layout;

static inline unsigned long task_rlimit(const struct task_struct *task,
					unsigned int limit)
{
	return READ_ONCE(task->signal->rlim[limit].rlim_cur);
}

static inline unsigned long rlimit(unsigned int limit)
{
	return task_rlimit(current, limit);
}

#endif
