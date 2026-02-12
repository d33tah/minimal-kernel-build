#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

#include <linux/sched.h>
#include <linux/uaccess.h>

struct task_struct;
/* struct rusage forward decl removed - unused */
union thread_union;
struct css_set;

struct kernel_clone_args {
	u64 flags;
	int __user *pidfd;
	int __user *child_tid;
	int __user *parent_tid;
	int exit_signal;
	unsigned long stack;
	unsigned long stack_size;
	unsigned long tls;
	pid_t *set_tid;
	 
	size_t set_tid_size;
	int cgroup;
	int io_thread;
	int kthread;
	int idle;
	int (*fn)(void *);
	void *fn_arg;
	struct cgroup *cgrp;
	struct css_set *cset;
};

extern rwlock_t tasklist_lock;

extern union thread_union init_thread_union;
extern struct task_struct init_task;

/* lockdep_tasklist_lock_is_held is never defined but used in
   rcu_dereference_check conditions (which use RCU_LOCKDEP_WARN, a no-op) */
#define lockdep_tasklist_lock_is_held() (1)

extern asmlinkage void schedule_tail(struct task_struct *prev);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_cgroup_fork(struct task_struct *p, struct kernel_clone_args *kargs);
/* sched_post_fork removed - empty function */

void __noreturn do_task_dead(void);
void __noreturn make_task_dead(int signr);

extern void proc_caches_init(void);

extern void fork_init(void);
/* release_task made static in kernel/exit.c */

extern int copy_thread(struct task_struct *, const struct kernel_clone_args *);

extern void flush_thread(void);

extern void exit_thread(struct task_struct *tsk);
/* do_group_exit now static in exit.c */

extern void exit_files(struct task_struct *);

struct mm_struct *copy_init_mm(void);
extern pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
extern pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags);
/* kernel_wait4 now static in exit.c */

/* free_task made static in kernel/fork.c */
/* sched_exec() macro removed - no callers */

void put_task_struct_rcu_user(struct task_struct *task);

extern int arch_task_struct_size __read_mostly;

static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

#endif  
