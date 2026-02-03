
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
#include <linux/pid_namespace.h>
#include <linux/cgroup.h>
#include <linux/task_work.h>
/* linux/tty.h removed - unused */

/* linux/uaccess.h removed - unused */
#include <asm/siginfo.h>
#include <asm/syscall.h>

/* sigqueue_cachep removed - was assigned but never read */

/* Removed: print_fatal_signals - never used */
/* sig_handler removed - inlined into single caller (~4 LOC) */

/* sig_ignored inlined into __send_signal_locked */

/* Simplified - _NSIG_WORDS is always 2 on x86-32 (~22 LOC) */
static inline bool has_pending_signals(sigset_t *signal, sigset_t *blocked)
{
	unsigned long ready;
	ready = signal->sig[1] & ~blocked->sig[1];
	ready |= signal->sig[0] & ~blocked->sig[0];
	return ready != 0;
}

#define PENDING(p, b) has_pending_signals(&(p)->signal, (b))

/* Removed: recalc_sigpending_and_wake - empty stub */

void recalc_sigpending(void)
{
	struct task_struct *t = current;
	if ((t->jobctl & (JOBCTL_PENDING_MASK | JOBCTL_TRAP_FREEZE)) ||
	    PENDING(&t->pending, &t->blocked) ||
	    PENDING(&t->signal->shared_pending, &t->blocked)) {
		set_tsk_thread_flag(t, TIF_SIGPENDING);
	} else {
		clear_thread_flag(TIF_SIGPENDING);
	}
}

void calculate_sigpending(void)
{
	spin_lock_irq(&current->sighand->siglock);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

/* Removed: print_dropped_signal - empty stub */
/* task_set_jobctl_pending removed - always returned false, callers simplified */

/* __sigqueue_alloc inlined into send_signal_locked (~5 LOC) */

/* Stub: init doesn't exit cleanly, no signal queue cleanup needed */
void flush_sigqueue(struct sigpending *queue)
{
	sigemptyset(&queue->signal);
}

void ignore_signals(struct task_struct *t)
{
	int i;

	for (i = 0; i < _NSIG; ++i)
		t->sighand->action[i].sa.sa_handler = SIG_IGN;
}

void flush_signal_handlers(struct task_struct *t, int force_default)
{
	int i;
	struct k_sigaction *ka = &t->sighand->action[0];
	for (i = _NSIG; i != 0; i--) {
		if (force_default || ka->sa.sa_handler != SIG_IGN)
			ka->sa.sa_handler = SIG_DFL;
		ka->sa.sa_flags = 0;
#ifdef __ARCH_HAS_SA_RESTORER
		ka->sa.sa_restorer = NULL;
#endif
		sigemptyset(&ka->sa.sa_mask);
		ka++;
	}
}

/* signal_wake_up_state, signal_wake_up removed - never called */

/* check_kill_permission inlined into kill_something_info caller */
/* complete_signal was empty stub - removed */
/* __send_signal_locked inlined into send_signal_locked */

int send_signal_locked(int sig, struct kernel_siginfo *info,
		       struct task_struct *t, enum pid_type type)
{
	/* Simplified stub for minimal hello-world kernel
	 * - Skip complex ignored signal check (init ignores most anyway)
	 * - Skip sigqueue allocation (no signal handlers to run)
	 * - Just mark signal as pending
	 */
	struct sigpending *pending;
	void __user *handler;

	/* Check if signal should be ignored */
	handler = t->sighand->action[sig - 1].sa.sa_handler;
	if (handler == SIG_IGN ||
	    (handler == SIG_DFL && sig_kernel_ignore(sig)))
		return 0;
	if (is_global_init(t) && sig_kernel_only(sig))
		return 0;

	pending = (type != PIDTYPE_PID) ? &t->signal->shared_pending :
					  &t->pending;

	/* Skip if already pending */
	if ((sig < SIGRTMIN) && sigismember(&pending->signal, sig))
		return 0;

	/* Just mark signal pending - no queue needed for simple init */
	sigaddset(&pending->signal, sig);
	return 0;
}

/* Removed: setup_print_fatal_signals and __setup - never used */

int do_send_sig_info(int sig, struct kernel_siginfo *info,
		     struct task_struct *p, enum pid_type type)
{
	unsigned long flags;
	int ret = -ESRCH;

	if (lock_task_sighand(p, &flags)) {
		ret = send_signal_locked(sig, info, p, type);
		unlock_task_sighand(p, &flags);
	}

	return ret;
}

enum sig_handler {
	HANDLER_CURRENT,
	HANDLER_SIG_DFL,
	HANDLER_EXIT,
};

static int force_sig_info_to_task(struct kernel_siginfo *info,
				  struct task_struct *t,
				  enum sig_handler handler)
{
	unsigned long int flags;
	int ret, blocked, ignored;
	struct k_sigaction *action;
	int sig = info->si_signo;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	action = &t->sighand->action[sig - 1];
	ignored = action->sa.sa_handler == SIG_IGN;
	blocked = sigismember(&t->blocked, sig);
	if (blocked || ignored || (handler != HANDLER_CURRENT)) {
		action->sa.sa_handler = SIG_DFL;
		if (handler == HANDLER_EXIT)
			action->sa.sa_flags |= SA_IMMUTABLE;
		if (blocked)
			sigdelset(&t->blocked, sig);
	}

	if (action->sa.sa_handler == SIG_DFL &&
	    (!t->ptrace || (handler == HANDLER_EXIT)))
		t->signal->flags &= ~SIGNAL_UNKILLABLE;
	ret = send_signal_locked(sig, info, t, PIDTYPE_PID);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);

	return ret;
}

int force_sig_info(struct kernel_siginfo *info)
{
	return force_sig_info_to_task(info, current, HANDLER_CURRENT);
}

/* zap_other_threads removed - never called (~5 LOC) */

struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
					   unsigned long *flags)
{
	struct sighand_struct *sighand;

	rcu_read_lock();
	sighand = rcu_dereference(tsk->sighand);
	if (sighand)
		spin_lock_irqsave(&sighand->siglock, *flags);
	rcu_read_unlock();

	return sighand;
}

int group_send_sig_info(int sig, struct kernel_siginfo *info,
			struct task_struct *p, enum pid_type type)
{
	int ret;

	rcu_read_lock();
	/* Simplified: else branches both set ret=0 */
	ret = valid_signal(sig) ? 0 : -EINVAL;
	rcu_read_unlock();

	if (!ret && sig)
		ret = do_send_sig_info(sig, info, p, type);

	return ret;
}

int send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p)
{
	if (!valid_signal(sig))
		return -EINVAL;

	return do_send_sig_info(sig, info, p, PIDTYPE_PID);
}

#define __si_special(priv) ((priv) ? SEND_SIG_PRIV : SEND_SIG_NOINFO)

int send_sig(int sig, struct task_struct *p, int priv)
{
	return send_sig_info(sig, __si_special(priv), p);
}

void force_sig(int sig)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info(&info);
}

void force_fatal_sig(int sig)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info_to_task(&info, current, HANDLER_SIG_DFL);
}

void force_exit_sig(int sig)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info_to_task(&info, current, HANDLER_EXIT);
}

/* Removed: force_sigsegv - empty stub */

int force_sig_fault(int sig, int code, void __user *addr)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = addr;
	/* ia64-specific fields removed - x86 only */
	return force_sig_info_to_task(&info, current, HANDLER_CURRENT);
}

/* force_sig_pkuerr, kill_pgrp, do_notify_parent, get_signal,
   signal_setup_done removed - never called */

int ptrace_notify(int exit_code, unsigned long message)
{
	BUG_ON((exit_code & (0x7f | ~0xffff)) != SIGTRAP);
	if (unlikely(task_work_pending(current)))
		task_work_run();

	/* ptrace_do_notify and ptrace_stop inlined away - lock/unlock was empty */
	return exit_code;
}

/* Removed: retarget_shared_pending - inlined into __set_current_blocked */

void exit_signals(struct task_struct *tsk)
{
	tsk->flags |= PF_EXITING;
}

/* Stub: restart_syscall not needed for Hello World */
SYSCALL_DEFINE0(restart_syscall)
{
	return -EINTR;
}

/* do_no_restart_syscall removed - restart_block removed from task_struct */

/* Stub: single-threaded init, no thread group signal retargeting */
void __set_current_blocked(const sigset_t *newset)
{
	struct task_struct *tsk = current;

	if (sigequalsets(&tsk->blocked, newset))
		return;

	spin_lock_irq(&tsk->sighand->siglock);
	tsk->blocked = *newset;
	recalc_sigpending();
	spin_unlock_irq(&tsk->sighand->siglock);
}

/* Stub: rt_sigprocmask not needed for Hello World */
SYSCALL_DEFINE4(rt_sigprocmask, int, how, sigset_t __user *, nset,
		sigset_t __user *, oset, size_t, sigsetsize)
{
	return 0;
}

/* Stub: rt_sigpending not needed for Hello World */
SYSCALL_DEFINE2(rt_sigpending, sigset_t __user *, uset, size_t, sigsetsize)
{
	return 0;
}

/* sigpending replaced with COND_SYSCALL */

#ifdef __ARCH_WANT_SYS_SIGPROCMASK

/* Stub: sigprocmask not needed for Hello World */
SYSCALL_DEFINE3(sigprocmask, int, how, old_sigset_t __user *, nset,
		old_sigset_t __user *, oset)
{
	return 0;
}
#endif

/* Stub: rt_sigaction not needed for Hello World */
/* CONFIG_ODD_RT_SIGACTION not set - #ifndef removed */
SYSCALL_DEFINE4(rt_sigaction, int, sig, const struct sigaction __user *, act,
		struct sigaction __user *, oact, size_t, sigsetsize)
{
	return 0;
}

/* Stub: sigaction not needed for Hello World */
SYSCALL_DEFINE3(sigaction, int, sig, const struct old_sigaction __user *, act,
		struct old_sigaction __user *, oact)
{
	return 0;
}

/* signal and pause syscalls replaced with COND_SYSCALL */

void __init signals_init(void)
{
	/* siginfo_buildtime_checks was empty stub - removed */
	/* sigqueue_cachep initialization removed - variable was never read */
}
