/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <uapi/asm/signal.h>
#include <linux/sched/task.h>

#include <linux/mm_types.h>

/*
 * NOTE! "signal_struct" does not have its own
 * locking, because a shared signal_struct always
 * implies a shared sighand_struct, so locking
 * sighand_struct is always a proper superset of
 * the locking of signal_struct.
 */
struct signal_struct {
	/*
	 * We don't bother to synchronize most readers of this at all,
	 * because there is no reader checking a limit that actually needs
	 * to get both rlim_cur and rlim_max atomically, and either one
	 * alone is a single word that can safely be read normally.
	 * getrlimit/setrlimit use task_lock(current->group_leader) to
	 * protect this instead of the siglock, because they really
	 * have no need to disable irqs.
	 */
	struct rlimit rlim[RLIM_NLIMITS];
	struct mutex cred_guard_mutex; /* guard against foreign influences on
					 * credential calculations
					 * (notably. ptrace)
					 * Deprecated do not use in new code.
					 * Use exec_update_lock instead.
					 */
	struct rw_semaphore exec_update_lock; /* Held while task_struct is
						 * being updated during exec,
						 * and may have inconsistent
						 * permissions.
						 */
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

#endif /* _LINUX_SCHED_SIGNAL_H */
