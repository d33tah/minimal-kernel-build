
#include <linux/slab.h>
#include <linux/sched/cputime.h>

static struct kmem_cache *sigqueue_cachep;

/* Removed: print_fatal_signals - never used */

static inline bool has_pending_signals(sigset_t *signal, sigset_t *blocked) {
	unsigned long ready;
	long i;

	switch (_NSIG_WORDS) {
	default:
		for (i = _NSIG_WORDS, ready = 0; --i >= 0 ;)
			ready |= signal->sig[i] &~ blocked->sig[i];
		break;

	case 4: ready  = signal->sig[3] &~ blocked->sig[3];
		ready |= signal->sig[2] &~ blocked->sig[2];
		ready |= signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 2: ready  = signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 1: ready  = signal->sig[0] &~ blocked->sig[0];
	}
	return ready !=	0;
}

#define PENDING(p,b) has_pending_signals(&(p)->signal, (b))

static bool recalc_sigpending_tsk(struct task_struct *t) {
	if ((t->jobctl & (JOBCTL_PENDING_MASK | JOBCTL_TRAP_FREEZE)) || PENDING(&t->pending, &t->blocked) || PENDING(&t->signal->shared_pending, &t->blocked)) {
		set_tsk_thread_flag(t, TIF_SIGPENDING);
		return true;
	}

	
	return false;
}

void recalc_sigpending(void) {
	if (!recalc_sigpending_tsk(current))
		clear_thread_flag(TIF_SIGPENDING);

}

void calculate_sigpending(void) {
	
	spin_lock_irq(&current->sighand->siglock);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

void ignore_signals(struct task_struct *t) {
	int i;

	for (i = 0; i < _NSIG; ++i)
		t->sighand->action[i].sa.sa_handler = SIG_IGN;
}

void
flush_signal_handlers(struct task_struct *t, int force_default) {
	int i;
	struct k_sigaction *ka = &t->sighand->action[0];
	for (i = _NSIG ; i != 0 ; i--) {
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


/*
 * Removed: signal_wake_up_state - unreachable. Its only caller was the
 * signal_wake_up() inline (sched/signal.h), whose only caller was
 * zap_other_threads(), reached only from the removed exit_group/do_group_exit
 * path. No signals are delivered to the single-thread init in this build.
 */

/* Removed: setup_print_fatal_signals and __setup - never used */

enum sig_handler { HANDLER_CURRENT, HANDLER_SIG_DFL, };

static int
force_sig_info_to_task(struct kernel_siginfo *info, struct task_struct *t, enum sig_handler handler) {
	/*
	 * Anchor-stub: runtime-dead. No fault/trap ever fires on this
	 * single-shot boot, so no forced signal is ever delivered. The whole
	 * private delivery subtree (__send_signal_locked/prepare_signal/
	 * sig_ignored/__sigqueue_alloc) has been deleted; nothing is queued.
	 */
	return 0;
}

/*
 * Removed: zap_other_threads - unreachable. Its only caller was the removed
 * do_group_exit() (exit_group syscall). The init task is single-threaded so
 * the multi-thread teardown is never reached.
 */

void force_sig(int sig) {
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info_to_task(&info, current, HANDLER_CURRENT);
}

void force_fatal_sig(int sig) {
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info_to_task(&info, current, HANDLER_SIG_DFL);
}

int force_sig_fault(int sig, int code, void __user *addr) {
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = addr;
	return force_sig_info_to_task(&info, current, HANDLER_CURRENT);
}

void exit_signals(struct task_struct *tsk) {
	/* Minimal stub: just mark as exiting */
	tsk->flags |= PF_EXITING;
}

void __init signals_init(void) {
	sigqueue_cachep = KMEM_CACHE(sigqueue, SLAB_PANIC | SLAB_ACCOUNT);
}

