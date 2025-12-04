#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H


#include <linux/sched.h>
#include <linux/uaccess.h>

struct task_struct;
struct rusage;
union thread_union;
struct css_set;

#define CLONE_LEGACY_FLAGS 0xffffffffULL

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
extern spinlock_t mmlist_lock;

extern union thread_union init_thread_union;
extern struct task_struct init_task;

extern int lockdep_tasklist_lock_is_held(void);

extern asmlinkage void schedule_tail(struct task_struct *prev);
extern void init_idle(struct task_struct *idle, int cpu);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_cgroup_fork(struct task_struct *p, struct kernel_clone_args *kargs);
extern void sched_post_fork(struct task_struct *p);
extern void sched_dead(struct task_struct *p);

void __noreturn do_task_dead(void);
void __noreturn make_task_dead(int signr);

extern void proc_caches_init(void);

extern void fork_init(void);

extern void release_task(struct task_struct * p);

extern int copy_thread(struct task_struct *, const struct kernel_clone_args *);

extern void flush_thread(void);

extern void exit_thread(struct task_struct *tsk);
extern __noreturn void do_group_exit(int);

extern void exit_files(struct task_struct *);
extern void exit_itimers(struct task_struct *);

extern pid_t kernel_clone(struct kernel_clone_args *kargs);
struct task_struct *create_io_thread(int (*fn)(void *), void *arg, int node);
struct task_struct *fork_idle(int);
struct mm_struct *copy_init_mm(void);
extern pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
extern pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags);
extern long kernel_wait4(pid_t, int __user *, int, struct rusage *);
int kernel_wait(pid_t pid, int *stat);

extern void free_task(struct task_struct *tsk);

#define sched_exec()   {}

static inline struct task_struct *get_task_struct(struct task_struct *t)
{
	refcount_inc(&t->usage);
	return t;
}

extern void __put_task_struct(struct task_struct *t);

static inline void put_task_struct(struct task_struct *t)
{
	if (refcount_dec_and_test(&t->usage))
		__put_task_struct(t);
}

/* put_task_struct_many removed - unused */

void put_task_struct_rcu_user(struct task_struct *task);

extern int arch_task_struct_size __read_mostly;


static inline struct vm_struct *task_stack_vm_area(const struct task_struct *t)
{
	return NULL;
}

static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

#endif  
