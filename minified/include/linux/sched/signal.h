#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/rwsem.h>

#include <linux/cred.h>
#include <linux/refcount.h>
#include <asm/ptrace.h>
struct sighand_struct {
	spinlock_t		siglock;
	refcount_t		count;
	struct k_sigaction	action[_NSIG];
};

struct signal_struct {
	struct sigpending	shared_pending;

	struct pid *pids[PIDTYPE_MAX];

	struct rlimit rlim[RLIM_NLIMITS];

	struct rw_semaphore exec_update_lock;
} __randomize_layout;

static inline int task_sigpending(struct task_struct *p)
{
	return unlikely(test_tsk_thread_flag(p,TIF_SIGPENDING));
}

static inline int signal_pending(struct task_struct *p)
{
	 
	if (unlikely(test_tsk_thread_flag(p, TIF_NOTIFY_SIGNAL)))
		return 1;
	return task_sigpending(p);
}

static inline int __fatal_signal_pending(struct task_struct *p)
{
	return unlikely(sigismember(&p->pending.signal, SIGKILL));
}

static inline int fatal_signal_pending(struct task_struct *p)
{
	return task_sigpending(p) && __fatal_signal_pending(p);
}

static inline int signal_pending_state(unsigned int state, struct task_struct *p)
{
	if (!(state & (TASK_INTERRUPTIBLE | TASK_WAKEKILL)))
		return 0;
	if (!signal_pending(p))
		return 0;

	return (state & TASK_INTERRUPTIBLE) || __fatal_signal_pending(p);
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
