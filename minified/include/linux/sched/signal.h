#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/rculist.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

#include <linux/cred.h>
#include <linux/mm_types.h>
#include <asm/ptrace.h>

struct sighand_struct {
	spinlock_t siglock;
	refcount_t count;
};

struct signal_struct {
	refcount_t sigcnt;
	struct list_head thread_head;
	struct pid *pids[PIDTYPE_MAX];
	struct rlimit rlim[RLIM_NLIMITS];
	struct mutex cred_guard_mutex;
	struct rw_semaphore exec_update_lock;
} __randomize_layout;

extern void __cleanup_sighand(struct sighand_struct *);

#define while_each_thread(g, t) while ((t = next_thread(t)) != g)

static inline struct pid *task_pgrp(struct task_struct *task)
{
	return task->signal->pids[PIDTYPE_PGID];
}

static inline struct pid *task_session(struct task_struct *task)
{
	return task->signal->pids[PIDTYPE_SID];
}

static inline bool thread_group_leader(struct task_struct *p)
{
	return p->exit_signal >= 0;
}

static inline struct task_struct *next_thread(const struct task_struct *p)
{
	return list_entry_rcu(p->thread_group.next, struct task_struct,
			      thread_group);
}

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
