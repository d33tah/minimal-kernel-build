
#include <linux/syscalls.h>
#include <linux/sched/signal.h>

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

void force_sig(int sig)
{
	panic("force_sig");
}
int force_sig_fault(int sig, int code, void __user *addr)
{
	panic("force_sig_fault");
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

/* signal and pause syscalls replaced with COND_SYSCALL */

void __init signals_init(void)
{
}
