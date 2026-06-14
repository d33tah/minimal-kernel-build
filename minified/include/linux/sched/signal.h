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
#define JOBCTL_PTRACE_FROZEN_BIT	24
#define JOBCTL_STOPPED_BIT	26
#define JOBCTL_TRACED_BIT	27
#define JOBCTL_STOP_PENDING	(1UL << JOBCTL_STOP_PENDING_BIT)
#define JOBCTL_TRAP_STOP	(1UL << JOBCTL_TRAP_STOP_BIT)
#define JOBCTL_TRAP_FREEZE	(1UL << JOBCTL_TRAP_FREEZE_BIT)
#define JOBCTL_PTRACE_FROZEN	(1UL << JOBCTL_PTRACE_FROZEN_BIT)
#define JOBCTL_STOPPED		(1UL << JOBCTL_STOPPED_BIT)
#define JOBCTL_TRACED		(1UL << JOBCTL_TRACED_BIT)
#define JOBCTL_TRAP_MASK	(JOBCTL_TRAP_STOP)
#define JOBCTL_PENDING_MASK	(JOBCTL_STOP_PENDING | JOBCTL_TRAP_MASK)
extern void task_clear_jobctl_pending(struct task_struct *task, unsigned long mask);
#include <linux/cred.h>
#include <linux/refcount.h>
#include <linux/posix-timers.h>
#include <linux/mm_types.h>
#include <asm/ptrace.h>


struct sighand_struct {
	spinlock_t		siglock;
	refcount_t		count;
	wait_queue_head_t	signalfd_wqh;
	struct k_sigaction	action[_NSIG];
};

struct multiprocess_signals {
	sigset_t signal;
	struct hlist_node node;
};

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

struct signal_struct {
	refcount_t		sigcnt;
	atomic_t		live;
	int			nr_threads;
	struct list_head	thread_head;

	wait_queue_head_t	wait_chldexit;	 

	 
	struct task_struct	*curr_target;

	 
	struct sigpending	shared_pending;

	 
	struct hlist_head	multiprocess;


	int			group_exit_code;


	int			group_stop_count;
	unsigned int		flags;  

	struct core_state *core_state;

	struct pid *pids[PIDTYPE_MAX];


	struct pid *tty_old_pgrp;

	 
	int leader;

	struct tty_struct *tty;  

	 
	seqlock_t stats_lock;
	u64 utime, stime;
	u64 gtime;
	struct prev_cputime prev_cputime;
	unsigned long nvcsw, nivcsw;
	unsigned long min_flt, maj_flt;
	unsigned long inblock, oublock;
	unsigned long maxrss;
	struct task_io_accounting ioac;

	 
	unsigned long long sum_sched_runtime;

	 
	struct rlimit rlim[RLIM_NLIMITS];


	 
	bool oom_flag_origin;
	short oom_score_adj;		 
	short oom_score_adj_min;	 
	struct mm_struct *oom_mm;	 

	struct mutex cred_guard_mutex;	 
	struct rw_semaphore exec_update_lock;	 
} __randomize_layout;

#define SIGNAL_GROUP_EXIT	0x00000004

#define SIGNAL_UNKILLABLE	0x00000040


extern void ignore_signals(struct task_struct *);
extern void flush_signal_handlers(struct task_struct *, int force_default);

#ifdef __ia64__
# define ___ARCH_SI_IA64(_a1, _a2, _a3) , _a1, _a2, _a3
#else
# define ___ARCH_SI_IA64(_a1, _a2, _a3)
#endif

int force_sig_fault(int sig, int code, void __user *addr
	___ARCH_SI_IA64(int imm, unsigned int flags, unsigned long isr));
/* send_sig_fault, force_sig_mceerr, send_sig_mceerr, force_sig_bnderr, send_sig_perf,
   force_sig_ptrace_errno_trap, force_sig_fault_trapno, send_sig_fault_trapno,
   force_sig_seccomp removed - unused */

extern int send_sig_info(int, struct kernel_siginfo *, struct task_struct *);
/* force_sigsegv now static in signal.c */
extern int force_sig_info(struct kernel_siginfo *);
extern __must_check bool do_notify_parent(struct task_struct *, int);
extern void force_sig(int);
extern void force_fatal_sig(int);
extern int send_sig(int, struct task_struct *, int);
extern int zap_other_threads(struct task_struct *p);

static inline bool __set_notify_signal(struct task_struct *task)
{
	return !test_and_set_tsk_thread_flag(task, TIF_NOTIFY_SIGNAL) &&
	       !wake_up_state(task, TASK_INTERRUPTIBLE);
}

static inline void set_notify_signal(struct task_struct *task)
{
	if (__set_notify_signal(task))
		kick_process(task);
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

static inline bool fault_signal_pending(vm_fault_t fault_flags,
					struct pt_regs *regs)
{
	return unlikely((fault_flags & VM_FAULT_RETRY) &&
			(fatal_signal_pending(current) ||
			 (user_mode(regs) && signal_pending(current))));
}

/* recalc_sigpending_and_wake only used internally in signal.c */
extern void recalc_sigpending(void);
extern void calculate_sigpending(void);

extern void signal_wake_up_state(struct task_struct *t, unsigned int state);

static inline void signal_wake_up(struct task_struct *t, bool fatal)
{
	unsigned int state = 0;
	if (fatal && !(t->jobctl & JOBCTL_PTRACE_FROZEN)) {
		t->jobctl &= ~(JOBCTL_STOPPED | JOBCTL_TRACED);
		state = TASK_WAKEKILL | __TASK_TRACED;
	}
	signal_wake_up_state(t, state);
}

#ifdef TIF_RESTORE_SIGMASK

static inline void set_restore_sigmask(void)
{
	set_thread_flag(TIF_RESTORE_SIGMASK);
}

static inline void clear_restore_sigmask(void)
{
	clear_thread_flag(TIF_RESTORE_SIGMASK);
}
static inline bool test_and_clear_restore_sigmask(void)
{
	return test_and_clear_thread_flag(TIF_RESTORE_SIGMASK);
}

#else	 

static inline void set_restore_sigmask(void)
{
	current->restore_sigmask = true;
}
static inline void clear_restore_sigmask(void)
{
	current->restore_sigmask = false;
}
static inline bool test_and_clear_restore_sigmask(void)
{
	if (!current->restore_sigmask)
		return false;
	current->restore_sigmask = false;
	return true;
}
#endif



#define SEND_SIG_NOINFO ((struct kernel_siginfo *) 0)
#define SEND_SIG_PRIV	((struct kernel_siginfo *) 1)

static inline int __on_sig_stack(unsigned long sp)
{
	return sp > current->sas_ss_sp &&
		sp - current->sas_ss_sp <= current->sas_ss_size;
}

static inline int on_sig_stack(unsigned long sp)
{
	 
	if (current->sas_ss_flags & SS_AUTODISARM)
		return 0;

	return __on_sig_stack(sp);
}

static inline int sas_ss_flags(unsigned long sp)
{
	if (!current->sas_ss_size)
		return SS_DISABLE;

	return on_sig_stack(sp) ? SS_ONSTACK : 0;
}

static inline void sas_ss_reset(struct task_struct *p)
{
	p->sas_ss_sp = 0;
	p->sas_ss_size = 0;
	p->sas_ss_flags = SS_DISABLE;
}

extern void __cleanup_sighand(struct sighand_struct *);

#define tasklist_empty() \
	list_empty(&init_task.tasks)

#define next_task(p) \
	list_entry_rcu((p)->tasks.next, struct task_struct, tasks)

#define for_each_process(p) \
	for (p = &init_task ; (p = next_task(p)) != &init_task ; )


#define do_each_thread(g, t) \
	for (g = t = &init_task ; (g = t = next_task(g)) != &init_task ; ) do

#define while_each_thread(g, t) \
	while ((t = next_thread(t)) != g)

#define __for_each_thread(signal, t)	\
	list_for_each_entry_rcu(t, &(signal)->thread_head, thread_node)

#define for_each_thread(p, t)		\
	__for_each_thread((p)->signal, t)




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

static inline int thread_group_empty(struct task_struct *p)
{
	return list_empty(&p->thread_group);
}

#define delay_group_leader(p) \
		(thread_group_leader(p) && !thread_group_empty(p))


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
