
#include <linux/syscalls.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
#include <linux/pid_namespace.h>
#include <linux/task_work.h>

#include <asm-generic/siginfo.h>
#include <asm/syscall.h>

/* Simplified - _NSIG_WORDS is always 2 on x86-32 (~22 LOC) */
static inline bool has_pending_signals(sigset_t *signal, sigset_t *blocked)
{
	unsigned long ready;
	ready = signal->sig[1] & ~blocked->sig[1];
	ready |= signal->sig[0] & ~blocked->sig[0];
	return ready != 0;
}

#define PENDING(p, b) has_pending_signals(&(p)->signal, (b))

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

static int send_signal_locked(int sig, struct kernel_siginfo *info,
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
	if (t->tgid == 1 && sig_kernel_only(sig)) /* is_global_init inlined */
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

enum sig_handler {
	HANDLER_CURRENT,
	HANDLER_SIG_DFL,
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
		if (blocked)
			sigdelset(&t->blocked, sig);
	}

	if (action->sa.sa_handler == SIG_DFL)
		ret = send_signal_locked(sig, info, t, PIDTYPE_PID);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);

	return ret;
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
	force_sig_info_to_task(&info, current, HANDLER_CURRENT);
}

int force_sig_fault(int sig, int code, void __user *addr)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = addr;
	return force_sig_info_to_task(&info, current, HANDLER_CURRENT);
}

/* force_sig_pkuerr, kill_pgrp, do_notify_parent, get_signal,
   signal_setup_done removed - never called */

SYSCALL_DEFINE0(restart_syscall)
{
	return -EINTR;
}

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

/* rt_sigprocmask, rt_sigpending, rt_sigaction, sigaction syscalls removed
   - not in syscall table */

/* signal and pause syscalls replaced with COND_SYSCALL */

void __init signals_init(void)
{
}
