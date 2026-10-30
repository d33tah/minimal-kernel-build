#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

#include <linux/sched.h>
#include <linux/uaccess.h>

struct task_struct;
union thread_union;

struct kernel_clone_args {
	u64 flags;
	int exit_signal;
	unsigned long stack;
	int (*fn)(void *);
	void *fn_arg;
};

extern rwlock_t tasklist_lock;

extern union thread_union init_thread_union;
extern struct task_struct init_task;

extern int lockdep_tasklist_lock_is_held(void);

extern asmlinkage void schedule_tail(struct task_struct *prev);
extern void init_idle(struct task_struct *idle, int cpu);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_cgroup_fork(struct task_struct *p,
			      struct kernel_clone_args *kargs);

extern void proc_caches_init(void);

extern void fork_init(void);

extern int copy_thread(struct task_struct *, const struct kernel_clone_args *);

extern void flush_thread(void);

/* do_group_exit now static in exit.c */

extern pid_t kernel_clone(struct kernel_clone_args *kargs);
extern pid_t user_mode_thread(int (*fn)(void *), void *arg,
			      unsigned long flags);
/* kernel_wait4 now static in exit.c */

extern void free_task(struct task_struct *tsk);

#define sched_exec() \
	{            \
	}

extern void __put_task_struct(struct task_struct *t);

static inline void put_task_struct(struct task_struct *t)
{
	if (refcount_dec_and_test(&t->usage))
		__put_task_struct(t);
}

void put_task_struct_rcu_user(struct task_struct *task);

static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

#endif
