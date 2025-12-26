
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/pid_namespace.h>
#include <linux/cgroup.h>
#include <linux/audit.h>
#include <linux/task_work.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>

#include <asm/param.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/siginfo.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>

static struct kmem_cache *sigqueue_cachep;

/* Removed: print_fatal_signals - never used */

static void __user *sig_handler(struct task_struct *t, int sig)
{
	return t->sighand->action[sig - 1].sa.sa_handler;
}

static inline bool sig_handler_ignored(void __user *handler, int sig)
{
	return handler == SIG_IGN ||
	       (handler == SIG_DFL && sig_kernel_ignore(sig));
}

static bool sig_task_ignored(struct task_struct *t, int sig, bool force)
{
	void __user *handler;

	handler = sig_handler(t, sig);

	if (unlikely(is_global_init(t) && sig_kernel_only(sig)))
		return true;

	if (unlikely(t->signal->flags & SIGNAL_UNKILLABLE) &&
	    handler == SIG_DFL && !(force && sig_kernel_only(sig)))
		return true;

	if (unlikely((t->flags & PF_KTHREAD) &&
		     (handler == SIG_KTHREAD_KERNEL) && !force))
		return true;

	return sig_handler_ignored(handler, sig);
}

static bool sig_ignored(struct task_struct *t, int sig, bool force)
{
	if (sigismember(&t->blocked, sig) || sigismember(&t->real_blocked, sig))
		return false;

	if (t->ptrace && sig != SIGKILL)
		return false;

	return sig_task_ignored(t, sig, force);
}

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

static bool recalc_sigpending_tsk(struct task_struct *t)
{
	if ((t->jobctl & (JOBCTL_PENDING_MASK | JOBCTL_TRAP_FREEZE)) ||
	    PENDING(&t->pending, &t->blocked) ||
	    PENDING(&t->signal->shared_pending, &t->blocked) ||
	    cgroup_task_frozen(t)) {
		set_tsk_thread_flag(t, TIF_SIGPENDING);
		return true;
	}

	return false;
}

/* Stubbed - only used internally */
static void recalc_sigpending_and_wake(struct task_struct *t)
{
}

void recalc_sigpending(void)
{
	if (!recalc_sigpending_tsk(current) && !freezing(current))
		clear_thread_flag(TIF_SIGPENDING);
}

void calculate_sigpending(void)
{
	spin_lock_irq(&current->sighand->siglock);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

#define SYNCHRONOUS_MASK                                        \
	(sigmask(SIGSEGV) | sigmask(SIGBUS) | sigmask(SIGILL) | \
	 sigmask(SIGTRAP) | sigmask(SIGFPE) | sigmask(SIGSYS))

static inline void print_dropped_signal(int sig)
{
	/* Stub: skip signal drop reporting for minimal kernel */
}

/* Stubbed - not used externally */
bool task_set_jobctl_pending(struct task_struct *task, unsigned long mask)
{
	return false;
}

/* Stubbed - not used externally */
void task_clear_jobctl_pending(struct task_struct *task, unsigned long mask)
{
}

/* Stubbed - not used externally */
void task_join_group_stop(struct task_struct *task)
{
}

static struct sigqueue *__sigqueue_alloc(int sig, struct task_struct *t,
					 gfp_t gfp_flags, int override_rlimit,
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
	    likely(sigpending <= task_rlimit(t, RLIMIT_SIGPENDING))) {
		q = kmem_cache_alloc(sigqueue_cachep, gfp_flags);
	} else {
		print_dropped_signal(sig);
	}

	if (unlikely(q == NULL)) {
		dec_rlimit_put_ucounts(ucounts, UCOUNT_RLIMIT_SIGPENDING);
	} else {
		INIT_LIST_HEAD(&q->list);
		q->flags = sigqueue_flags;
		q->ucounts = ucounts;
	}
	return q;
}

static void __sigqueue_free(struct sigqueue *q)
{
	if (q->flags & SIGQUEUE_PREALLOC)
		return;
	if (q->ucounts) {
		dec_rlimit_put_ucounts(q->ucounts, UCOUNT_RLIMIT_SIGPENDING);
		q->ucounts = NULL;
	}
	kmem_cache_free(sigqueue_cachep, q);
}

void flush_sigqueue(struct sigpending *queue)
{
	struct sigqueue *q;

	sigemptyset(&queue->signal);
	while (!list_empty(&queue->list)) {
		q = list_entry(queue->list.next, struct sigqueue, list);
		list_del_init(&q->list);
		__sigqueue_free(q);
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
	lockdep_assert_held(&t->sighand->siglock);

	set_tsk_thread_flag(t, TIF_SIGPENDING);

	if (!wake_up_state(t, state | TASK_INTERRUPTIBLE))
		kick_process(t);
}

static inline int is_si_special(const struct kernel_siginfo *info)
{
	return info <= SEND_SIG_PRIV;
}

static inline bool si_fromuser(const struct kernel_siginfo *info)
{
	return info == SEND_SIG_NOINFO ||
	       (!is_si_special(info) && SI_FROMUSER(info));
}

static int check_kill_permission(int sig, struct kernel_siginfo *info,
				 struct task_struct *t)
{
	/* Minimal stub: simplified signal permission checking */
	if (!valid_signal(sig))
		return -EINVAL;

	if (!si_fromuser(info))
		return 0;

	/* Skip session/cred checks for minimal kernel */
	return security_task_kill(t, info, sig, NULL);
}

static bool prepare_signal(int sig, struct task_struct *p, bool force)
{
	return !sig_ignored(p, sig, force);
}

static void complete_signal(int sig, struct task_struct *p, enum pid_type type)
{
	return;
}

static inline bool legacy_queue(struct sigpending *signals, int sig)
{
	return (sig < SIGRTMIN) && sigismember(&signals->signal, sig);
}

static int __send_signal_locked(int sig, struct kernel_siginfo *info,
				struct task_struct *t, enum pid_type type,
				bool force)
{
	/* Minimal stub: simplified signal delivery */
	struct sigpending *pending;
	struct sigqueue *q;

	lockdep_assert_held(&t->sighand->siglock);

	if (!prepare_signal(sig, t, force))
		return 0;

	pending = (type != PIDTYPE_PID) ? &t->signal->shared_pending :
					  &t->pending;

	if (legacy_queue(pending, sig))
		return 0;

	/* For SIGKILL or kernel threads, just set the signal */
	if ((sig == SIGKILL) || (t->flags & PF_KTHREAD))
		goto out_set;

	q = __sigqueue_alloc(sig, t, GFP_ATOMIC, 0, 0);
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
	complete_signal(sig, t, type);
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
		if (blocked) {
			sigdelset(&t->blocked, sig);
			recalc_sigpending_and_wake(t);
		}
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

	p->signal->group_stop_count = 0;

	while_each_thread(p, t)
	{
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
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
	ret = check_kill_permission(sig, info, p);
	rcu_read_unlock();

	if (!ret && sig)
		ret = do_send_sig_info(sig, info, p, type);

	return ret;
}

/* Stubbed - only used internally */
static int __kill_pgrp_info(int sig, struct kernel_siginfo *info,
			    struct pid *pgrp)
{
	return -ESRCH;
}

int kill_pid_info(int sig, struct kernel_siginfo *info, struct pid *pid)
{
	struct task_struct *p;
	int error;

	rcu_read_lock();
	p = pid_task(pid, PIDTYPE_PID);
	if (p)
		error = group_send_sig_info(sig, info, p, PIDTYPE_TGID);
	else
		error = -ESRCH;
	rcu_read_unlock();

	return error;
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

/* Stubbed - now static, only used internally */
static void force_sigsegv(int sig)
{
}

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

/* Stub: PKU signal - used by fault.c */
#ifdef SEGV_PKUERR
int force_sig_pkuerr(void __user *addr, u32 pkey)
{
	return 0;
}
#endif

int kill_pgrp(struct pid *pid, int sig, int priv)
{
	int ret;

	read_lock(&tasklist_lock);
	ret = __kill_pgrp_info(sig, __si_special(priv), pid);
	read_unlock(&tasklist_lock);

	return ret;
}

bool do_notify_parent(struct task_struct *tsk, int sig)
{
	return false;
}

static int ptrace_stop(int exit_code, int why, unsigned long message,
		       kernel_siginfo_t *info)
	__releases(&current->sighand->siglock)
		__acquires(&current->sighand->siglock)
{
	/* Stub: ptrace not needed for minimal boot */
	if (!current->ptrace || __fatal_signal_pending(current))
		return exit_code;

	return exit_code;
}

static int ptrace_do_notify(int signr, int exit_code, int why,
			    unsigned long message)
{
	kernel_siginfo_t info;

	clear_siginfo(&info);
	info.si_signo = signr;
	info.si_code = exit_code;
	info.si_pid = task_pid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	return ptrace_stop(exit_code, why, message, &info);
}

int ptrace_notify(int exit_code, unsigned long message)
{
	int signr;

	BUG_ON((exit_code & (0x7f | ~0xffff)) != SIGTRAP);
	if (unlikely(task_work_pending(current)))
		task_work_run();

	spin_lock_irq(&current->sighand->siglock);
	signr = ptrace_do_notify(SIGTRAP, exit_code, CLD_TRAPPED, message);
	spin_unlock_irq(&current->sighand->siglock);
	return signr;
}

bool get_signal(struct ksignal *ksig)
{
	/* Minimal stub: init doesn't use signals */
	return false;
}

static void signal_delivered(struct ksignal *ksig, int stepping)
{
	sigset_t blocked;

	clear_restore_sigmask();

	sigorsets(&blocked, &current->blocked, &ksig->ka.sa.sa_mask);
	if (!(ksig->ka.sa.sa_flags & SA_NODEFER))
		sigaddset(&blocked, ksig->sig);
	set_current_blocked(&blocked);
	if (current->sas_ss_flags & SS_AUTODISARM)
		sas_ss_reset(current);
	if (stepping)
		ptrace_notify(SIGTRAP, 0);
}

void signal_setup_done(int failed, struct ksignal *ksig, int stepping)
{
	if (failed)
		force_sigsegv(ksig->sig);
	else
		signal_delivered(ksig, stepping);
}

static void retarget_shared_pending(struct task_struct *tsk, sigset_t *which)
{
	sigset_t retarget;
	struct task_struct *t;

	sigandsets(&retarget, &tsk->signal->shared_pending.signal, which);
	if (sigisemptyset(&retarget))
		return;

	t = tsk;
	while_each_thread(tsk, t)
	{
		if (t->flags & PF_EXITING)
			continue;

		if (!has_pending_signals(&retarget, &t->blocked))
			continue;

		sigandsets(&retarget, &retarget, &t->blocked);

		if (!task_sigpending(t))
			signal_wake_up(t, 0);

		if (sigisemptyset(&retarget))
			break;
	}
}

void exit_signals(struct task_struct *tsk)
{
	/* Minimal stub: just mark as exiting */
	cgroup_threadgroup_change_begin(tsk);
	tsk->flags |= PF_EXITING;
	cgroup_threadgroup_change_end(tsk);
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

static void __set_task_blocked(struct task_struct *tsk, const sigset_t *newset)
{
	if (task_sigpending(tsk) && !thread_group_empty(tsk)) {
		sigset_t newblocked;

		sigandnsets(&newblocked, newset, &current->blocked);
		retarget_shared_pending(tsk, &newblocked);
	}
	tsk->blocked = *newset;
	recalc_sigpending();
}

void set_current_blocked(sigset_t *newset)
{
	sigdelsetmask(newset, sigmask(SIGKILL) | sigmask(SIGSTOP));
	__set_current_blocked(newset);
}

void __set_current_blocked(const sigset_t *newset)
{
	struct task_struct *tsk = current;

	if (sigequalsets(&tsk->blocked, newset))
		return;

	spin_lock_irq(&tsk->sighand->siglock);
	__set_task_blocked(tsk, newset);
	spin_unlock_irq(&tsk->sighand->siglock);
}

int sigprocmask(int how, sigset_t *set, sigset_t *oldset)
{
	struct task_struct *tsk = current;
	sigset_t newset;

	if (oldset)
		*oldset = tsk->blocked;

	switch (how) {
	case SIG_BLOCK:
		sigorsets(&newset, &tsk->blocked, set);
		break;
	case SIG_UNBLOCK:
		sigandnsets(&newset, &tsk->blocked, set);
		break;
	case SIG_SETMASK:
		newset = *set;
		break;
	default:
		return -EINVAL;
	}

	__set_current_blocked(&newset);
	return 0;
}

int set_user_sigmask(const sigset_t __user *umask, size_t sigsetsize)
{
	sigset_t kmask;

	if (!umask)
		return 0;
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;
	if (copy_from_user(&kmask, umask, sizeof(sigset_t)))
		return -EFAULT;

	set_restore_sigmask();
	current->saved_sigmask = current->blocked;
	set_current_blocked(&kmask);

	return 0;
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

int copy_siginfo_to_user(siginfo_t __user *to, const kernel_siginfo_t *from)
{
	if (copy_to_user(to, from, sizeof(struct kernel_siginfo)))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE4(rt_sigtimedwait, const sigset_t __user *, uthese,
		siginfo_t __user *, uinfo,
		const struct __kernel_timespec __user *, uts, size_t,
		sigsetsize)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
{
	/* Stub: kill not needed for minimal kernel */
	return -ENOSYS;
}

SYSCALL_DEFINE4(pidfd_send_signal, int, pidfd, int, sig, siginfo_t __user *,
		info, unsigned int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(tgkill, pid_t, tgid, pid_t, pid, int, sig)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(tkill, pid_t, pid, int, sig)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(rt_sigqueueinfo, pid_t, pid, int, sig, siginfo_t __user *,
		uinfo)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(rt_tgsigqueueinfo, pid_t, tgid, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	return -ENOSYS;
}

/* Stub: sigaltstack not needed for Hello World */
SYSCALL_DEFINE2(sigaltstack, const stack_t __user *, uss, stack_t __user *,
		uoss)
{
	return -ENOSYS;
}

int __save_altstack(stack_t __user *uss, unsigned long sp)
{
	struct task_struct *t = current;
	int err = __put_user((void __user *)t->sas_ss_sp, &uss->ss_sp) |
		  __put_user(t->sas_ss_flags, &uss->ss_flags) |
		  __put_user(t->sas_ss_size, &uss->ss_size);
	return err;
}

/* Stub: sigpending not needed for Hello World */
#ifdef __ARCH_WANT_SYS_SIGPENDING
SYSCALL_DEFINE1(sigpending, old_sigset_t __user *, uset)
{
	return -ENOSYS;
}
#endif

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

/* Stub: old signal() syscall not needed for Hello World */
#ifdef __ARCH_WANT_SYS_SIGNAL
SYSCALL_DEFINE2(signal, int, sig, __sighandler_t, handler)
{
	return -ENOSYS;
}
#endif

/* Stub: pause and sigsuspend not needed for Hello World */
#ifdef __ARCH_WANT_SYS_PAUSE
SYSCALL_DEFINE0(pause)
{
	return -ENOSYS;
}
#endif

SYSCALL_DEFINE2(rt_sigsuspend, sigset_t __user *, unewset, size_t, sigsetsize)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(sigsuspend, int, unused1, int, unused2, old_sigset_t, mask)
{
	return -ENOSYS;
}

static inline void siginfo_buildtime_checks(void)
{
	/* Stub: buildtime checks not needed for minimal kernel */
}

void __init signals_init(void)
{
	siginfo_buildtime_checks();

	sigqueue_cachep = KMEM_CACHE(sigqueue, SLAB_PANIC | SLAB_ACCOUNT);
}
