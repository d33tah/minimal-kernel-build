#ifndef _LINUX_SCHED_SIGNAL_H
#define _LINUX_SCHED_SIGNAL_H

#include <linux/rculist.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

/* Inlined from sched/jobctl.h - trimmed to used flags only */
#define JOBCTL_STOP_PENDING_BIT	17
#define JOBCTL_TRAP_STOP_BIT	19
#define JOBCTL_TRAP_FREEZE_BIT	23
#define JOBCTL_STOP_PENDING	(1UL << JOBCTL_STOP_PENDING_BIT)
#define JOBCTL_TRAP_STOP	(1UL << JOBCTL_TRAP_STOP_BIT)
#define JOBCTL_TRAP_FREEZE	(1UL << JOBCTL_TRAP_FREEZE_BIT)
#define JOBCTL_TRAP_MASK	(JOBCTL_TRAP_STOP)
#define JOBCTL_PENDING_MASK	(JOBCTL_STOP_PENDING | JOBCTL_TRAP_MASK)
/* task_set_jobctl_pending removed - always returned false, callers simplified */
#include <linux/cred.h>
#include <linux/refcount.h>
#include <linux/mm_types.h>
#include <asm/ptrace.h>


struct sighand_struct {
	spinlock_t		siglock;
	refcount_t		count;
	/* signalfd_wqh removed - only initialized, never used */
	struct k_sigaction	action[_NSIG];
};

/* pacct_struct, cpu_itimer, task_cputime_atomic, thread_group_cputimer removed - never used */

struct multiprocess_signals {
	sigset_t signal;
	struct hlist_node node;
};

struct signal_struct {
	refcount_t		sigcnt;
	atomic_t		live;
	int			nr_threads;
	struct list_head	thread_head;

	/* wait_chldexit removed - only initialized, never used */

	struct task_struct	*curr_target;

	 
	struct sigpending	shared_pending;

	 
	struct hlist_head	multiprocess;

	 
	int			group_exit_code;
	 
	int			notify_count;
	struct task_struct	*group_exec_task;

	/* group_stop_count removed - write-only field, never read */
	unsigned int		flags;


	unsigned int		is_child_subreaper:1;
	unsigned int		has_child_subreaper:1;


	struct pid *pids[PIDTYPE_MAX];

	struct tty_struct *tty;

	/* stats_lock removed - protected fields that no longer exist */
	/* utime, stime, gtime, prev_cputime, nvcsw, nivcsw, min_flt, maj_flt,
	 * maxrss, sum_sched_runtime all removed - write-only fields */


	struct rlimit rlim[RLIM_NLIMITS];


	/* oom_flag_origin, oom_score_adj, oom_score_adj_min, oom_mm removed - never set */

	struct mutex cred_guard_mutex;	 
	struct rw_semaphore exec_update_lock;	 
} __randomize_layout;

/* SIGNAL_GROUP_EXIT removed - unused */
#define SIGNAL_UNKILLABLE	0x00000040
/* SIGNAL_STOP_*, SIGNAL_CLD_*, SIGNAL_STOP_MASK removed - never used */


extern void ignore_signals(struct task_struct *);
extern void flush_signal_handlers(struct task_struct *, int force_default);

/* ___ARCH_SI_IA64 removed - x86 only, no ia64 support */

int force_sig_fault(int sig, int code, void __user *addr);
/* send_sig_fault, force_sig_mceerr, send_sig_mceerr, force_sig_bnderr, send_sig_perf,
   force_sig_ptrace_errno_trap, force_sig_fault_trapno, send_sig_fault_trapno,
   force_sig_seccomp, force_sig_pkuerr removed - unused */

/* send_sig_info, kill_pid_info, do_notify_parent removed - never called */
/* force_sigsegv now static in signal.c */
extern int force_sig_info(struct kernel_siginfo *);
extern void force_sig(int);
extern void force_fatal_sig(int);
extern void force_exit_sig(int);
extern int send_sig(int, struct task_struct *, int);
/* zap_other_threads removed - never called */

static inline bool __set_notify_signal(struct task_struct *task)
{
	return !test_and_set_tsk_thread_flag(task, TIF_NOTIFY_SIGNAL) &&
	       !wake_up_state(task, TASK_INTERRUPTIBLE);
}

static inline void set_notify_signal(struct task_struct *task)
{
	__set_notify_signal(task);
	/* kick_process call removed - empty stub */
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
extern void calculate_sigpending(void);

/* signal_wake_up_state, signal_wake_up, task_join_group_stop removed - never called */

/* x86 doesn't define TIF_RESTORE_SIGMASK, use current->restore_sigmask */
/* set_restore_sigmask, clear_restore_sigmask, test_restore_sigmask removed - not called */
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

/* sigmask_to_save removed - never called */

#define SEND_SIG_NOINFO ((struct kernel_siginfo *) 0)
#define SEND_SIG_PRIV	((struct kernel_siginfo *) 1)

/* __on_sig_stack, on_sig_stack, sas_ss_flags, sas_ss_reset removed - never called or empty stubs */

extern void __cleanup_sighand(struct sighand_struct *);

/* next_task, for_each_process, do_each_thread removed - never called */

#define while_each_thread(g, t) \
	while ((t = next_thread(t)) != g)

#define __for_each_thread(signal, t)	\
	list_for_each_entry_rcu(t, &(signal)->thread_head, thread_node)

#define for_each_thread(p, t)		\
	__for_each_thread((p)->signal, t)


/* task_pid_type removed - unused */

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

static inline
bool same_thread_group(struct task_struct *p1, struct task_struct *p2)
{
	return p1->signal == p2->signal;
}

static inline struct task_struct *next_thread(const struct task_struct *p)
{
	return list_entry_rcu(p->thread_group.next,
			      struct task_struct, thread_group);
}

/* thread_group_empty removed - never called (single-threaded init) */

extern struct sighand_struct *__lock_task_sighand(struct task_struct *task,
							unsigned long *flags);

static inline struct sighand_struct *lock_task_sighand(struct task_struct *task,
						       unsigned long *flags)
{
	struct sighand_struct *ret;

	ret = __lock_task_sighand(task, flags);
	(void)__cond_lock(&task->sighand->siglock, ret);
	return ret;
}

static inline void unlock_task_sighand(struct task_struct *task,
						unsigned long *flags)
{
	spin_unlock_irqrestore(&task->sighand->siglock, *flags);
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
