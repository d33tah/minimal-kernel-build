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
#include <linux/cred.h>
#include <linux/mm_types.h>
#include <asm/ptrace.h>


struct sighand_struct { spinlock_t		siglock; refcount_t		count; struct k_sigaction	action[_NSIG]; };

struct multiprocess_signals { sigset_t signal; };

struct signal_struct { refcount_t		sigcnt; atomic_t		live; struct list_head	thread_head; struct sigpending	shared_pending; unsigned int		flags; struct pid *pids[PIDTYPE_MAX]; struct tty_struct *tty; struct rlimit rlim[RLIM_NLIMITS]; struct mutex cred_guard_mutex; struct rw_semaphore exec_update_lock; } __randomize_layout;

#define SIGNAL_UNKILLABLE	0x00000040


extern void ignore_signals(struct task_struct *);
extern void flush_signal_handlers(struct task_struct *, int force_default);

int force_sig_fault(int sig, int code, void __user *addr);
/* send_sig_fault, force_sig_mceerr, send_sig_mceerr, force_sig_bnderr, send_sig_perf,
   force_sig_ptrace_errno_trap, force_sig_fault_trapno, send_sig_fault_trapno,
   force_sig_seccomp removed - unused */

/* force_sigsegv now static in signal.c */
extern void force_sig(int);
extern void force_fatal_sig(int);
/* zap_other_threads removed: only caller was do_group_exit (exit_group), gone */

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

static inline bool fault_signal_pending(vm_fault_t fault_flags, struct pt_regs *regs)
{
	return unlikely((fault_flags & VM_FAULT_RETRY) && (fatal_signal_pending(current) || (user_mode(regs) && signal_pending(current))));
}

/* recalc_sigpending_and_wake only used internally in signal.c */
extern void recalc_sigpending(void);
extern void calculate_sigpending(void);

/*
 * signal_wake_up_state + the signal_wake_up() inline removed: the only caller
 * chain (zap_other_threads <- do_group_exit <- exit_group syscall) is gone.
 */



/* SEND_SIG_NOINFO/SEND_SIG_PRIV removed - 0-caller signal-send magic pointers */

extern void __cleanup_sighand(struct sighand_struct *);

#define while_each_thread(g, t) 	while ((t = next_thread(t)) != g)






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
	return list_entry_rcu(p->thread_group.next, struct task_struct, thread_group);
}

static inline unsigned long task_rlimit(const struct task_struct *task, unsigned int limit)
{
	return READ_ONCE(task->signal->rlim[limit].rlim_cur);
}

static inline unsigned long rlimit(unsigned int limit)
{
	return task_rlimit(current, limit);
}

#endif  
