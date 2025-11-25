 

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
#include <linux/coredump.h>
#include <linux/cn_proc.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>

#include <trace/events/signal.h>

#include <asm/param.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/siginfo.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>	

static struct kmem_cache *sigqueue_cachep;

int print_fatal_signals __read_mostly;

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

/* Stubbed - not used externally */
void recalc_sigpending_and_wake(struct task_struct *t)
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

#define SYNCHRONOUS_MASK \
	(sigmask(SIGSEGV) | sigmask(SIGBUS) | sigmask(SIGILL) | \
	 sigmask(SIGTRAP) | sigmask(SIGFPE) | sigmask(SIGSYS))

int next_signal(struct sigpending *pending, sigset_t *mask)
{
	unsigned long i, *s, *m, x;
	int sig = 0;

	s = pending->signal.sig;
	m = mask->sig;

	
	x = *s &~ *m;
	if (x) {
		if (x & SYNCHRONOUS_MASK)
			x &= SYNCHRONOUS_MASK;
		sig = ffz(~x) + 1;
		return sig;
	}

	switch (_NSIG_WORDS) {
	default:
		for (i = 1; i < _NSIG_WORDS; ++i) {
			x = *++s &~ *++m;
			if (!x)
				continue;
			sig = ffz(~x) + i*_NSIG_BPW + 1;
			break;
		}
		break;

	case 2:
		x = s[1] &~ m[1];
		if (!x)
			break;
		sig = ffz(~x) + _NSIG_BPW + 1;
		break;

	case 1:
		
		break;
	}

	return sig;
}

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
void task_clear_jobctl_trapping(struct task_struct *task)
{
}

/* Stubbed - not used externally */
void task_clear_jobctl_pending(struct task_struct *task, unsigned long mask)
{
}

/* Stubbed - not used externally */
void task_join_group_stop(struct task_struct *task)
{
}

static struct sigqueue *
__sigqueue_alloc(int sig, struct task_struct *t, gfp_t gfp_flags,
		 int override_rlimit, const unsigned int sigqueue_flags)
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

	if (override_rlimit || likely(sigpending <= task_rlimit(t, RLIMIT_SIGPENDING))) {
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
		q = list_entry(queue->list.next, struct sigqueue , list);
		list_del_init(&q->list);
		__sigqueue_free(q);
	}
}

/* Stubbed - not used externally */
void flush_signals(struct task_struct *t)
{
}

void ignore_signals(struct task_struct *t)
{
	int i;

	for (i = 0; i < _NSIG; ++i)
		t->sighand->action[i].sa.sa_handler = SIG_IGN;
}

void
flush_signal_handlers(struct task_struct *t, int force_default)
{
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

bool unhandled_signal(struct task_struct *tsk, int sig)
{
	void __user *handler = tsk->sighand->action[sig-1].sa.sa_handler;
	if (is_global_init(tsk))
		return true;

	if (handler != SIG_IGN && handler != SIG_DFL)
		return false;

	
	return !tsk->ptrace;
}

static void collect_signal(int sig, struct sigpending *list, kernel_siginfo_t *info,
			   bool *resched_timer)
{
	struct sigqueue *q, *first = NULL;

	
	list_for_each_entry(q, &list->list, list) {
		if (q->info.si_signo == sig) {
			if (first)
				goto still_pending;
			first = q;
		}
	}

	sigdelset(&list->signal, sig);

	if (first) {
still_pending:
		list_del_init(&first->list);
		copy_siginfo(info, &first->info);

		*resched_timer =
			(first->flags & SIGQUEUE_PREALLOC) &&
			(info->si_code == SI_TIMER) &&
			(info->si_sys_private);

		__sigqueue_free(first);
	} else {
		
		clear_siginfo(info);
		info->si_signo = sig;
		info->si_errno = 0;
		info->si_code = SI_USER;
		info->si_pid = 0;
		info->si_uid = 0;
	}
}

static int __dequeue_signal(struct sigpending *pending, sigset_t *mask,
			kernel_siginfo_t *info, bool *resched_timer)
{
	int sig = next_signal(pending, mask);

	if (sig)
		collect_signal(sig, pending, info, resched_timer);
	return sig;
}

/* Stub: dequeue_signal not used externally */
int dequeue_signal(struct task_struct *tsk, sigset_t *mask,
		   kernel_siginfo_t *info, enum pid_type *type)
{
	*type = PIDTYPE_PID;
	return 0;
}

void signal_wake_up_state(struct task_struct *t, unsigned int state)
{
	lockdep_assert_held(&t->sighand->siglock);

	set_tsk_thread_flag(t, TIF_SIGPENDING);

	
	if (!wake_up_state(t, state | TASK_INTERRUPTIBLE))
		kick_process(t);
}

static void flush_sigqueue_mask(sigset_t *mask, struct sigpending *s)
{
	struct sigqueue *q, *n;
	sigset_t m;

	sigandsets(&m, mask, &s->signal);
	if (sigisemptyset(&m))
		return;

	sigandnsets(&s->signal, &s->signal, mask);
	list_for_each_entry_safe(q, n, &s->list, list) {
		if (sigismember(mask, q->info.si_signo)) {
			list_del_init(&q->list);
			__sigqueue_free(q);
		}
	}
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
				struct task_struct *t, enum pid_type type, bool force)
{
	/* Minimal stub: simplified signal delivery */
	struct sigpending *pending;
	struct sigqueue *q;

	lockdep_assert_held(&t->sighand->siglock);

	if (!prepare_signal(sig, t, force))
		return 0;

	pending = (type != PIDTYPE_PID) ? &t->signal->shared_pending : &t->pending;

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

static int __init setup_print_fatal_signals(char *str)
{
	/* Stub: skip setup for minimal kernel */
	return 1;
}

__setup("print-fatal-signals=", setup_print_fatal_signals);

int do_send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p,
			enum pid_type type)
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

static int
force_sig_info_to_task(struct kernel_siginfo *info, struct task_struct *t,
	enum sig_handler handler)
{
	unsigned long int flags;
	int ret, blocked, ignored;
	struct k_sigaction *action;
	int sig = info->si_signo;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	action = &t->sighand->action[sig-1];
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

	while_each_thread(p, t) {
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

int __kill_pgrp_info(int sig, struct kernel_siginfo *info, struct pid *pgrp)
{
	struct task_struct *p = NULL;
	int retval, success;

	success = 0;
	retval = -ESRCH;
	do_each_pid_task(pgrp, PIDTYPE_PGID, p) {
		int err = group_send_sig_info(sig, info, p, PIDTYPE_PGID);
		success |= !err;
		retval = err;
	} while_each_pid_task(pgrp, PIDTYPE_PGID, p);
	return success ? 0 : retval;
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

static int kill_proc_info(int sig, struct kernel_siginfo *info, pid_t pid)
{
	int error;
	rcu_read_lock();
	error = kill_pid_info(sig, info, find_vpid(pid));
	rcu_read_unlock();
	return error;
}

static inline bool kill_as_cred_perm(const struct cred *cred,
				     struct task_struct *target)
{
	const struct cred *pcred = __task_cred(target);

	return uid_eq(cred->euid, pcred->suid) ||
	       uid_eq(cred->euid, pcred->uid) ||
	       uid_eq(cred->uid, pcred->suid) ||
	       uid_eq(cred->uid, pcred->uid);
}

/* Stubbed - not used externally */
int kill_pid_usb_asyncio(int sig, int errno, sigval_t addr,
			 struct pid *pid, const struct cred *cred)
{
	return -ENOSYS;
}

static int kill_something_info(int sig, struct kernel_siginfo *info, pid_t pid)
{
	/* Simplified: only handle simple pid > 0 case */
	if (pid > 0)
		return kill_proc_info(sig, info, pid);

	/* For process groups and broadcast, just return error */
	return -ESRCH;
}

int send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p)
{
	
	if (!valid_signal(sig))
		return -EINVAL;

	return do_send_sig_info(sig, info, p, PIDTYPE_PID);
}

#define __si_special(priv) \
	((priv) ? SEND_SIG_PRIV : SEND_SIG_NOINFO)

int
send_sig(int sig, struct task_struct *p, int priv)
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

/* Stubbed - not used externally */
void force_sigsegv(int sig)
{
}

int force_sig_fault_to_task(int sig, int code, void __user *addr
	___ARCH_SI_IA64(int imm, unsigned int flags, unsigned long isr)
	, struct task_struct *t)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = addr;
#ifdef __ia64__
	info.si_imm = imm;
	info.si_flags = flags;
	info.si_isr = isr;
#endif
	return force_sig_info_to_task(&info, t, HANDLER_CURRENT);
}

int force_sig_fault(int sig, int code, void __user *addr
	___ARCH_SI_IA64(int imm, unsigned int flags, unsigned long isr))
{
	return force_sig_fault_to_task(sig, code, addr
				       ___ARCH_SI_IA64(imm, flags, isr), current);
}

int send_sig_fault(int sig, int code, void __user *addr
	___ARCH_SI_IA64(int imm, unsigned int flags, unsigned long isr)
	, struct task_struct *t)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = addr;
#ifdef __ia64__
	info.si_imm = imm;
	info.si_flags = flags;
	info.si_isr = isr;
#endif
	return send_sig_info(info.si_signo, &info, t);
}

/* Stub: memory corrected error signals not used in minimal kernel */
int force_sig_mceerr(int code, void __user *addr, short lsb) { return 0; }
int send_sig_mceerr(int code, void __user *addr, short lsb, struct task_struct *t) { return 0; }
int force_sig_bnderr(void __user *addr, void __user *lower, void __user *upper) { return 0; }

#ifdef SEGV_PKUERR
int force_sig_pkuerr(void __user *addr, u32 pkey)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code  = SEGV_PKUERR;
	info.si_addr  = addr;
	info.si_pkey  = pkey;
	return force_sig_info(&info);
}
#endif

/* Stub: perf/seccomp/ptrace signals not used in minimal kernel */
int send_sig_perf(void __user *addr, u32 type, u64 sig_data) { return 0; }
int force_sig_seccomp(int syscall, int reason, bool force_coredump) { return 0; }
int force_sig_ptrace_errno_trap(int errno, void __user *addr) { return 0; }

/* Stub: trapno signals not used in minimal kernel */
int force_sig_fault_trapno(int sig, int code, void __user *addr, int trapno) { return 0; }
int send_sig_fault_trapno(int sig, int code, void __user *addr, int trapno, struct task_struct *t) { return 0; }

int kill_pgrp(struct pid *pid, int sig, int priv)
{
	int ret;

	read_lock(&tasklist_lock);
	ret = __kill_pgrp_info(sig, __si_special(priv), pid);
	read_unlock(&tasklist_lock);

	return ret;
}

int kill_pid(struct pid *pid, int sig, int priv)
{
	return kill_pid_info(sig, __si_special(priv), pid);
}

/* Stub: sigqueue_alloc not used externally */
struct sigqueue *sigqueue_alloc(void)
{
	return NULL;
}

/* Stub: sigqueue_free not used externally */
void sigqueue_free(struct sigqueue *q)
{
}

/* Stub: send_sigqueue not used externally */
int send_sigqueue(struct sigqueue *q, struct pid *pid, enum pid_type type)
{
	return -1;
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

static int ptrace_do_notify(int signr, int exit_code, int why, unsigned long message)
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
	while_each_thread(tsk, t) {
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

SYSCALL_DEFINE0(restart_syscall)
{
	struct restart_block *restart = &current->restart_block;
	return restart->fn(restart);
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

SYSCALL_DEFINE4(rt_sigprocmask, int, how, sigset_t __user *, nset,
		sigset_t __user *, oset, size_t, sigsetsize)
{
	sigset_t old_set, new_set;
	int error;

	
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	old_set = current->blocked;

	if (nset) {
		if (copy_from_user(&new_set, nset, sizeof(sigset_t)))
			return -EFAULT;
		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		error = sigprocmask(how, &new_set, NULL);
		if (error)
			return error;
	}

	if (oset) {
		if (copy_to_user(oset, &old_set, sizeof(sigset_t)))
			return -EFAULT;
	}

	return 0;
}

static void do_sigpending(sigset_t *set)
{
	spin_lock_irq(&current->sighand->siglock);
	sigorsets(set, &current->pending.signal,
		  &current->signal->shared_pending.signal);
	spin_unlock_irq(&current->sighand->siglock);

	
	sigandsets(set, &current->blocked, set);
}

SYSCALL_DEFINE2(rt_sigpending, sigset_t __user *, uset, size_t, sigsetsize)
{
	sigset_t set;

	if (sigsetsize > sizeof(*uset))
		return -EINVAL;

	do_sigpending(&set);

	if (copy_to_user(uset, &set, sigsetsize))
		return -EFAULT;

	return 0;
}

enum siginfo_layout siginfo_layout(unsigned sig, int si_code)
{
	return SIL_KILL;
}

int copy_siginfo_to_user(siginfo_t __user *to, const kernel_siginfo_t *from)
{
	if (copy_to_user(to, from, sizeof(struct kernel_siginfo)))
		return -EFAULT;
	return 0;
}

int copy_siginfo_from_user(kernel_siginfo_t *to, const siginfo_t __user *from)
{
	if (copy_from_user(to, from, sizeof(struct kernel_siginfo)))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE4(rt_sigtimedwait, const sigset_t __user *, uthese,
		siginfo_t __user *, uinfo,
		const struct __kernel_timespec __user *, uts,
		size_t, sigsetsize)
{
	return -ENOSYS;
}

static inline void prepare_kill_siginfo(int sig, struct kernel_siginfo *info)
{
	clear_siginfo(info);
	info->si_signo = sig;
	info->si_errno = 0;
	info->si_code = SI_USER;
	info->si_pid = task_tgid_vnr(current);
	info->si_uid = from_kuid_munged(current_user_ns(), current_uid());
}

SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
{
	struct kernel_siginfo info;

	prepare_kill_siginfo(sig, &info);

	return kill_something_info(sig, &info, pid);
}

SYSCALL_DEFINE4(pidfd_send_signal, int, pidfd, int, sig,
		siginfo_t __user *, info, unsigned int, flags)
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

SYSCALL_DEFINE3(rt_sigqueueinfo, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(rt_tgsigqueueinfo, pid_t, tgid, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	return -ENOSYS;
}

void kernel_sigaction(int sig, __sighandler_t action)
{
	spin_lock_irq(&current->sighand->siglock);
	current->sighand->action[sig - 1].sa.sa_handler = action;
	if (action == SIG_IGN) {
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, sig);

		flush_sigqueue_mask(&mask, &current->signal->shared_pending);
		flush_sigqueue_mask(&mask, &current->pending);
		recalc_sigpending();
	}
	spin_unlock_irq(&current->sighand->siglock);
}

void __weak sigaction_compat_abi(struct k_sigaction *act,
		struct k_sigaction *oact)
{
}

int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact)
{
	struct k_sigaction *k;

	if (!valid_signal(sig) || sig < 1 || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &current->sighand->action[sig-1];

	spin_lock_irq(&current->sighand->siglock);
	if (k->sa.sa_flags & SA_IMMUTABLE) {
		spin_unlock_irq(&current->sighand->siglock);
		return -EINVAL;
	}
	if (oact)
		*oact = *k;

	if (act) {
		act->sa.sa_flags &= UAPI_SA_FLAGS;
		sigdelsetmask(&act->sa.sa_mask, sigmask(SIGKILL) | sigmask(SIGSTOP));
		*k = *act;
	}

	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

static int
do_sigaltstack (const stack_t *ss, stack_t *oss, unsigned long sp,
		size_t min_ss_size)
{
	/* Minimal stub: just handle reads */
	if (oss) {
		memset(oss, 0, sizeof(stack_t));
		oss->ss_flags = SS_DISABLE;
	}
	return 0;
}

SYSCALL_DEFINE2(sigaltstack,const stack_t __user *,uss, stack_t __user *,uoss)
{
	stack_t new, old;
	int err;
	if (uss && copy_from_user(&new, uss, sizeof(stack_t)))
		return -EFAULT;
	err = do_sigaltstack(uss ? &new : NULL, uoss ? &old : NULL,
			      current_user_stack_pointer(),
			      MINSIGSTKSZ);
	if (!err && uoss && copy_to_user(uoss, &old, sizeof(stack_t)))
		err = -EFAULT;
	return err;
}

int restore_altstack(const stack_t __user *uss)
{
	stack_t new;
	if (copy_from_user(&new, uss, sizeof(stack_t)))
		return -EFAULT;
	(void)do_sigaltstack(&new, NULL, current_user_stack_pointer(),
			     MINSIGSTKSZ);
	
	return 0;
}

int __save_altstack(stack_t __user *uss, unsigned long sp)
{
	struct task_struct *t = current;
	int err = __put_user((void __user *)t->sas_ss_sp, &uss->ss_sp) |
		__put_user(t->sas_ss_flags, &uss->ss_flags) |
		__put_user(t->sas_ss_size, &uss->ss_size);
	return err;
}

#ifdef __ARCH_WANT_SYS_SIGPENDING

SYSCALL_DEFINE1(sigpending, old_sigset_t __user *, uset)
{
	sigset_t set;

	if (sizeof(old_sigset_t) > sizeof(*uset))
		return -EINVAL;

	do_sigpending(&set);

	if (copy_to_user(uset, &set, sizeof(old_sigset_t)))
		return -EFAULT;

	return 0;
}

#endif

#ifdef __ARCH_WANT_SYS_SIGPROCMASK

SYSCALL_DEFINE3(sigprocmask, int, how, old_sigset_t __user *, nset,
		old_sigset_t __user *, oset)
{
	old_sigset_t old_set, new_set;
	sigset_t new_blocked;

	old_set = current->blocked.sig[0];

	if (nset) {
		if (copy_from_user(&new_set, nset, sizeof(*nset)))
			return -EFAULT;

		new_blocked = current->blocked;

		switch (how) {
		case SIG_BLOCK:
			sigaddsetmask(&new_blocked, new_set);
			break;
		case SIG_UNBLOCK:
			sigdelsetmask(&new_blocked, new_set);
			break;
		case SIG_SETMASK:
			new_blocked.sig[0] = new_set;
			break;
		default:
			return -EINVAL;
		}

		set_current_blocked(&new_blocked);
	}

	if (oset) {
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			return -EFAULT;
	}

	return 0;
}
#endif 

#ifndef CONFIG_ODD_RT_SIGACTION

SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct sigaction __user *, act,
		struct sigaction __user *, oact,
		size_t, sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret;

	
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (act && copy_from_user(&new_sa.sa, act, sizeof(new_sa.sa)))
		return -EFAULT;

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);
	if (ret)
		return ret;

	if (oact && copy_to_user(oact, &old_sa.sa, sizeof(old_sa.sa)))
		return -EFAULT;

	return 0;
}
#endif 

SYSCALL_DEFINE3(sigaction, int, sig,
		const struct old_sigaction __user *, act,
	        struct old_sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
#ifdef __ARCH_HAS_KA_RESTORER
		new_ka.ka_restorer = NULL;
#endif
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}

#ifdef __ARCH_WANT_SYS_SIGNAL

SYSCALL_DEFINE2(signal, int, sig, __sighandler_t, handler)
{
	struct k_sigaction new_sa, old_sa;
	int ret;

	new_sa.sa.sa_handler = handler;
	new_sa.sa.sa_flags = SA_ONESHOT | SA_NOMASK;
	sigemptyset(&new_sa.sa.sa_mask);

	ret = do_sigaction(sig, &new_sa, &old_sa);

	return ret ? ret : (unsigned long)old_sa.sa.sa_handler;
}
#endif 

#ifdef __ARCH_WANT_SYS_PAUSE

SYSCALL_DEFINE0(pause)
{
	while (!signal_pending(current)) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return -ERESTARTNOHAND;
}

#endif

static int sigsuspend(sigset_t *set)
{
	current->saved_sigmask = current->blocked;
	set_current_blocked(set);

	while (!signal_pending(current)) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	set_restore_sigmask();
	return -ERESTARTNOHAND;
}

SYSCALL_DEFINE2(rt_sigsuspend, sigset_t __user *, unewset, size_t, sigsetsize)
{
	sigset_t newset;

	
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	return sigsuspend(&newset);
}
 

SYSCALL_DEFINE3(sigsuspend, int, unused1, int, unused2, old_sigset_t, mask)
{
	sigset_t blocked;
	siginitset(&blocked, mask);
	return sigsuspend(&blocked);
}

__weak const char *arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
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

