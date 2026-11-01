/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

#include <linux/uaccess.h>

struct task_struct;
union thread_union;

struct kernel_clone_args {
	u64 flags;
	int (*fn)(void *);
	void *fn_arg;
};

extern union thread_union init_thread_union;
extern struct task_struct init_task;

extern asmlinkage void schedule_tail(struct task_struct *prev);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);

void __noreturn make_task_dead(int signr);

extern int copy_thread(struct task_struct *, const struct kernel_clone_args *);
extern pid_t user_mode_thread(int (*fn)(void *), void *arg,
			      unsigned long flags);
/* kernel_wait4 now static in exit.c */

static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

#endif /* _LINUX_SCHED_TASK_H */
