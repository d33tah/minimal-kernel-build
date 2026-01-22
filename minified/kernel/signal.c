
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/pid_namespace.h>
#include <linux/cgroup.h>
#include <linux/task_work.h>
#include <linux/tty.h>

#include <linux/uaccess.h>
#include <asm/siginfo.h>
#include <asm/syscall.h>

static struct kmem_cache *sigqueue_cachep;

/* Removed: print_fatal_signals - never used */
/* sig_handler removed - inlined into single caller (~4 LOC) */

/* sig_ignored inlined into __send_signal_locked */

static inline bool has_pending_signals(sigset_t *signal, sigset_t *blocked)
{
	unsigned long ready;
	long i;

	switch (_NSIG_WORDS) {
	default:
		for (i = _NSIG_WORDS, ready = 0; --i >= 0;)
			ready |= signal->sig[i] & ~blocked->sig[i];
		break;

	case 4:
		ready = signal->sig[3] & ~blocked->sig[3];
		ready |= signal->sig[2] & ~blocked->sig[2];
		ready |= signal->sig[1] & ~blocked->sig[1];
		ready |= signal->sig[0] & ~blocked->sig[0];
		break;

	case 2:
		ready = signal->sig[1] & ~blocked->sig[1];
		ready |= signal->sig[0] & ~blocked->sig[0];
		break;

	case 1:
		ready = signal->sig[0] & ~blocked->sig[0];
	}
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

static struct sigqueue *__sigqueue_alloc(struct task_struct *t, gfp_t gfp_flags,
					 int override_rlimit,
					 const unsigned int sigqueue_flags)
{
	struct sigqueue *q = NULL;
	struct ucounts *ucounts = NULL;
	long sigpending;

	rcu_read_lock();
	ucounts = task_ucounts(t);
	sigpending = inc_rlimit_get_ucounts(ucounts, UCOUNT_RLIMIT_SIGPENDING);
	rcu_read_unlock();
	if (!sigpending)
		return NULL;

	if (override_rlimit ||
	    likely(sigpending <= task_rlimit(t, RLIMIT_SIGPENDING)))
		q = kmem_cache_alloc(sigqueue_cachep, gfp_flags);

	if (unlikely(q == NULL)) {
		dec_rlimit_put_ucounts(ucounts, UCOUNT_RLIMIT_SIGPENDING);
	} else {
		INIT_LIST_HEAD(&q->list);
		q->flags = sigqueue_flags;
		q->ucounts = ucounts;
	}
	return q;
}

void flush_sigqueue(struct sigpending *queue)
{
	struct sigqueue *q;

	sigemptyset(&queue->signal);
	while (!list_empty(&queue->list)) {
		q = list_entry(queue->list.next, struct sigqueue, list);
		list_del_init(&q->list);
		/* Inlined __sigqueue_free */
		if (!(q->flags & SIGQUEUE_PREALLOC)) {
			if (q->ucounts) {
				dec_rlimit_put_ucounts(
					q->ucounts, UCOUNT_RLIMIT_SIGPENDING);
				q->ucounts = NULL;
			}
			kmem_cache_free(sigqueue_cachep, q);
		}
	}
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

void signal_wake_up_state(struct task_struct *t, unsigned int state)
{
	set_tsk_thread_flag(t, TIF_SIGPENDING);
	wake_up_state(t, state | TASK_INTERRUPTIBLE);
}

/* check_kill_permission inlined into kill_something_info caller */
/* complete_signal was empty stub - removed */

static int __send_signal_locked(int sig, struct kernel_siginfo *info,
				struct task_struct *t, enum pid_type type,
				bool force)
{
	/* Minimal stub: simplified signal delivery */
	struct sigpending *pending;
	struct sigqueue *q;

	/* Inlined sig_ignored */
	{
		void __user *handler;
		bool ignored = false;

		if (!sigismember(&t->blocked, sig) &&
		    !sigismember(&t->real_blocked, sig) &&
		    !(t->ptrace && sig != SIGKILL)) {
			handler = t->sighand->action[sig - 1].sa.sa_handler;

			if (unlikely(is_global_init(t) && sig_kernel_only(sig)))
				ignored = true;
			else if (unlikely(t->signal->flags &
					  SIGNAL_UNKILLABLE) &&
				 handler == SIG_DFL &&
				 !(force && sig_kernel_only(sig)))
				ignored = true;
			else if (unlikely((t->flags & PF_KTHREAD) &&
					  (handler == SIG_KTHREAD_KERNEL) &&
					  !force))
				ignored = true;
			else if (handler == SIG_IGN ||
				 (handler == SIG_DFL && sig_kernel_ignore(sig)))
				ignored = true;
		}
		if (ignored)
			return 0;
	}

	pending = (type != PIDTYPE_PID) ? &t->signal->shared_pending :
					  &t->pending;

	if ((sig < SIGRTMIN) && sigismember(&pending->signal, sig))
		return 0;

	/* For SIGKILL or kernel threads, just set the signal */
	if ((sig == SIGKILL) || (t->flags & PF_KTHREAD))
		goto out_set;

	q = __sigqueue_alloc(t, GFP_ATOMIC, 0, 0);
	if (q) {
		list_add_tail(&q->list, &pending->list);
		if (info == SEND_SIG_NOINFO) {
			clear_siginfo(&q->info);
			q->info.si_signo = sig;
			q->info.si_code = SI_USER;
		} else if (info != SEND_SIG_PRIV) {
			copy_siginfo(&q->info, info);
		}
	}

out_set:
	sigaddset(&pending->signal, sig);
	/* complete_signal was empty stub - call removed */
	return 0;
}

int send_signal_locked(int sig, struct kernel_siginfo *info,
		       struct task_struct *t, enum pid_type type)
{
	/* Minimal stub: simplified signal permission handling */
	bool force = false;

	if (info == SEND_SIG_PRIV)
		force = true;
	else if (info != SEND_SIG_NOINFO && info->si_code == SI_KERNEL)
		force = true;

	return __send_signal_locked(sig, info, t, type, force);
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

int zap_other_threads(struct task_struct *p)
{
	struct task_struct *t = p;
	int count = 0;
	/* group_stop_count assignment removed - field is write-only */

	while_each_thread(p, t)
	{
		count++;

		if (t->exit_state)
			continue;
		sigaddset(&t->pending.signal, SIGKILL);
		signal_wake_up(t, 1);
	}

	return count;
}

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

long do_no_restart_syscall(struct restart_block *param)
{
	return -EINTR;
}

void __set_current_blocked(const sigset_t *newset)
{
	struct task_struct *tsk = current;

	if (sigequalsets(&tsk->blocked, newset))
		return;

	spin_lock_irq(&tsk->sighand->siglock);
	/* Inlined __set_task_blocked and retarget_shared_pending */
	if (task_sigpending(tsk) && !thread_group_empty(tsk)) {
		sigset_t newblocked;
		sigset_t retarget;
		struct task_struct *t;
		sigandnsets(&newblocked, newset, &current->blocked);
		sigandsets(&retarget, &tsk->signal->shared_pending.signal,
			   &newblocked);
		if (!sigisemptyset(&retarget)) {
			t = tsk;
			while_each_thread(tsk, t)
			{
				if (t->flags & PF_EXITING)
					continue;
				if (!has_pending_signals(&retarget,
							 &t->blocked))
					continue;
				sigandsets(&retarget, &retarget, &t->blocked);
				if (!task_sigpending(t))
					signal_wake_up(t, 0);
				if (sigisemptyset(&retarget))
					break;
			}
		}
	}
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
#ifndef CONFIG_ODD_RT_SIGACTION
SYSCALL_DEFINE4(rt_sigaction, int, sig, const struct sigaction __user *, act,
		struct sigaction __user *, oact, size_t, sigsetsize)
{
	return 0;
}
#endif

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
	sigqueue_cachep = KMEM_CACHE(sigqueue, SLAB_PANIC | SLAB_ACCOUNT);
}
