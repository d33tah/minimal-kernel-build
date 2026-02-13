#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/rculist.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

#define JOBCTL_STOP_PENDING_BIT	17
#define JOBCTL_TRAP_STOP_BIT	19
#define JOBCTL_TRAP_FREEZE_BIT	23
#define JOBCTL_STOP_PENDING	(1UL << JOBCTL_STOP_PENDING_BIT)
#define JOBCTL_TRAP_STOP	(1UL << JOBCTL_TRAP_STOP_BIT)
#define JOBCTL_TRAP_FREEZE	(1UL << JOBCTL_TRAP_FREEZE_BIT)
#define JOBCTL_TRAP_MASK	(JOBCTL_TRAP_STOP)
#define JOBCTL_PENDING_MASK	(JOBCTL_STOP_PENDING | JOBCTL_TRAP_MASK)
#include <linux/cred.h>
#include <linux/refcount.h>
#include <linux/mm_types.h>
#include <asm/ptrace.h>

struct sighand_struct {
	spinlock_t		siglock;
	refcount_t		count;
	struct k_sigaction	action[_NSIG];
};

struct signal_struct {
	refcount_t		sigcnt;
	atomic_t		live;
	struct list_head	thread_head;

	struct sigpending	shared_pending;

	struct pid *pids[PIDTYPE_MAX];

	struct tty_struct *tty;

	/* utime, stime, gtime, prev_cputime, nvcsw, nivcsw, min_flt, maj_flt,
	 * maxrss, sum_sched_runtime all removed - write-only fields */

	struct rlimit rlim[RLIM_NLIMITS];

	struct mutex cred_guard_mutex;	 
	struct rw_semaphore exec_update_lock;	 
} __randomize_layout;

int force_sig_fault(int sig, int code, void __user *addr);
/* send_sig_fault, force_sig_mceerr, send_sig_mceerr, force_sig_bnderr, send_sig_perf,
   force_sig_ptrace_errno_trap, force_sig_fault_trapno, send_sig_fault_trapno,
   force_sig_seccomp, force_sig_pkuerr removed - unused */

/* force_sigsegv now static in signal.c */
extern void force_sig(int);

static inline bool __set_notify_signal(struct task_struct *task)
{
	return !test_and_set_tsk_thread_flag(task, TIF_NOTIFY_SIGNAL) &&
	       !wake_up_state(task, TASK_INTERRUPTIBLE);
}

static inline void set_notify_signal(struct task_struct *task)
{
	__set_notify_signal(task);
}

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

/* fault_signal_pending inlined at arch/x86/mm/fault.c - single caller */

/* recalc_sigpending_and_wake only used internally in signal.c */
extern void recalc_sigpending(void);

/* x86 doesn't define TIF_RESTORE_SIGMASK, use current->restore_sigmask */
static inline bool test_and_clear_restore_sigmask(void)
{
	if (!current->restore_sigmask)
		return false;
	current->restore_sigmask = false;
	return true;
}

static inline void restore_saved_sigmask(void)
{
	if (test_and_clear_restore_sigmask())
		__set_current_blocked(&current->saved_sigmask);
}

extern void __cleanup_sighand(struct sighand_struct *);

/* while_each_thread, __for_each_thread, for_each_thread, next_task,
 * for_each_process, do_each_thread removed - never called */

static inline struct pid *task_pgrp(struct task_struct *task)
{
	return task->signal->pids[PIDTYPE_PGID];
}

static inline struct pid *task_session(struct task_struct *task)
{
	return task->signal->pids[PIDTYPE_SID];
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
