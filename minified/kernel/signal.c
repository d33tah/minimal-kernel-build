 

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

void recalc_sigpending_and_wake(struct task_struct *t)
{
	if (recalc_sigpending_tsk(t))
		signal_wake_up(t, 0);
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
	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 10);

	if (!print_fatal_signals)
		return;

	if (!__ratelimit(&ratelimit_state))
		return;
}

bool task_set_jobctl_pending(struct task_struct *task, unsigned long mask)
{
	BUG_ON(mask & ~(JOBCTL_PENDING_MASK | JOBCTL_STOP_CONSUME |
			JOBCTL_STOP_SIGMASK | JOBCTL_TRAPPING));
	BUG_ON((mask & JOBCTL_TRAPPING) && !(mask & JOBCTL_PENDING_MASK));

	if (unlikely(fatal_signal_pending(task) || (task->flags & PF_EXITING)))
		return false;

	if (mask & JOBCTL_STOP_SIGMASK)
		task->jobctl &= ~JOBCTL_STOP_SIGMASK;

	task->jobctl |= mask;
	return true;
}

void task_clear_jobctl_trapping(struct task_struct *task)
{
	if (unlikely(task->jobctl & JOBCTL_TRAPPING)) {
		task->jobctl &= ~JOBCTL_TRAPPING;
		smp_mb();	
		wake_up_bit(&task->jobctl, JOBCTL_TRAPPING_BIT);
	}
}

void task_clear_jobctl_pending(struct task_struct *task, unsigned long mask)
{
	BUG_ON(mask & ~JOBCTL_PENDING_MASK);

	if (mask & JOBCTL_STOP_PENDING)
		mask |= JOBCTL_STOP_CONSUME | JOBCTL_STOP_DEQUEUED;

	task->jobctl &= ~mask;

	if (!(task->jobctl & JOBCTL_PENDING_MASK))
		task_clear_jobctl_trapping(task);
}

static bool task_participate_group_stop(struct task_struct *task)
{
	struct signal_struct *sig = task->signal;
	bool consume = task->jobctl & JOBCTL_STOP_CONSUME;

	WARN_ON_ONCE(!(task->jobctl & JOBCTL_STOP_PENDING));

	task_clear_jobctl_pending(task, JOBCTL_STOP_PENDING);

	if (!consume)
		return false;

	if (!WARN_ON_ONCE(sig->group_stop_count == 0))
		sig->group_stop_count--;

	
	if (!sig->group_stop_count && !(sig->flags & SIGNAL_STOP_STOPPED)) {
		signal_set_stop_flags(sig, SIGNAL_STOP_STOPPED);
		return true;
	}
	return false;
}

void task_join_group_stop(struct task_struct *task)
{
	unsigned long mask = current->jobctl & JOBCTL_STOP_SIGMASK;
	struct signal_struct *sig = current->signal;

	if (sig->group_stop_count) {
		sig->group_stop_count++;
		mask |= JOBCTL_STOP_CONSUME;
	} else if (!(sig->flags & SIGNAL_STOP_STOPPED))
		return;

	
	task_set_jobctl_pending(task, mask | JOBCTL_STOP_PENDING);
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

void flush_signals(struct task_struct *t)
{
	unsigned long flags;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	clear_tsk_thread_flag(t, TIF_SIGPENDING);
	flush_sigqueue(&t->pending);
	flush_sigqueue(&t->signal->shared_pending);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);
}

void ignore_signals(struct task_struct *t)
{
	int i;

	for (i = 0; i < _NSIG; ++i)
		t->sighand->action[i].sa.sa_handler = SIG_IGN;

	flush_signals(t);
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

int dequeue_signal(struct task_struct *tsk, sigset_t *mask,
		   kernel_siginfo_t *info, enum pid_type *type)
{
	bool resched_timer = false;
	int signr;

	
	*type = PIDTYPE_PID;
	signr = __dequeue_signal(&tsk->pending, mask, info, &resched_timer);
	if (!signr) {
		*type = PIDTYPE_TGID;
		signr = __dequeue_signal(&tsk->signal->shared_pending,
					 mask, info, &resched_timer);
	}

	recalc_sigpending();
	if (!signr)
		return 0;

	if (unlikely(sig_kernel_stop(signr))) {
		
		current->jobctl |= JOBCTL_STOP_DEQUEUED;
	}
	return signr;
}

static int dequeue_synchronous_signal(kernel_siginfo_t *info)
{
	struct task_struct *tsk = current;
	struct sigpending *pending = &tsk->pending;
	struct sigqueue *q, *sync = NULL;

	
	if (!((pending->signal.sig[0] & ~tsk->blocked.sig[0]) & SYNCHRONOUS_MASK))
		return 0;

	
	list_for_each_entry(q, &pending->list, list) {
		
		if ((q->info.si_code > SI_USER) &&
		    (sigmask(q->info.si_signo) & SYNCHRONOUS_MASK)) {
			sync = q;
			goto next;
		}
	}
	return 0;
next:
	
	list_for_each_entry_continue(q, &pending->list, list) {
		if (q->info.si_signo == sync->info.si_signo)
			goto still_pending;
	}

	sigdelset(&pending->signal, sync->info.si_signo);
	recalc_sigpending();
still_pending:
	list_del_init(&sync->list);
	copy_siginfo(info, &sync->info);
	__sigqueue_free(sync);
	return info->si_signo;
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

static bool kill_ok_by_cred(struct task_struct *t)
{
	const struct cred *cred = current_cred();
	const struct cred *tcred = __task_cred(t);

	return uid_eq(cred->euid, tcred->suid) ||
	       uid_eq(cred->euid, tcred->uid) ||
	       uid_eq(cred->uid, tcred->suid) ||
	       uid_eq(cred->uid, tcred->uid) ||
	       ns_capable(tcred->user_ns, CAP_KILL);
}

static int check_kill_permission(int sig, struct kernel_siginfo *info,
				 struct task_struct *t)
{
	struct pid *sid;
	int error;

	if (!valid_signal(sig))
		return -EINVAL;

	if (!si_fromuser(info))
		return 0;

	error = audit_signal_info(sig, t); 
	if (error)
		return error;

	if (!same_thread_group(current, t) &&
	    !kill_ok_by_cred(t)) {
		switch (sig) {
		case SIGCONT:
			sid = task_session(t);
			
			if (!sid || sid == task_session(current))
				break;
			fallthrough;
		default:
			return -EPERM;
		}
	}

	return security_task_kill(t, info, sig, NULL);
}

static void ptrace_trap_notify(struct task_struct *t)
{
	WARN_ON_ONCE(!(t->ptrace & PT_SEIZED));
	lockdep_assert_held(&t->sighand->siglock);

	task_set_jobctl_pending(t, JOBCTL_TRAP_NOTIFY);
	ptrace_signal_wake_up(t, t->jobctl & JOBCTL_LISTENING);
}

static bool prepare_signal(int sig, struct task_struct *p, bool force)
{
	return !sig_ignored(p, sig, force);
}

static inline bool wants_signal(int sig, struct task_struct *p)
{
	if (sigismember(&p->blocked, sig))
		return false;

	if (p->flags & PF_EXITING)
		return false;

	if (sig == SIGKILL)
		return true;

	if (task_is_stopped_or_traced(p))
		return false;

	return task_curr(p) || !task_sigpending(p);
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

static inline bool has_si_pid_and_uid(struct kernel_siginfo *info)
{
	bool ret = false;
	switch (siginfo_layout(info->si_signo, info->si_code)) {
	case SIL_KILL:
	case SIL_CHLD:
	case SIL_RT:
		ret = true;
		break;
	case SIL_TIMER:
	case SIL_POLL:
	case SIL_FAULT:
	case SIL_FAULT_TRAPNO:
	case SIL_FAULT_MCEERR:
	case SIL_FAULT_BNDERR:
	case SIL_FAULT_PKUERR:
	case SIL_FAULT_PERF_EVENT:
	case SIL_SYS:
		ret = false;
		break;
	}
	return ret;
}

int send_signal_locked(int sig, struct kernel_siginfo *info,
		       struct task_struct *t, enum pid_type type)
{
	
	bool force = false;

	if (info == SEND_SIG_NOINFO) {
		
		force = !task_pid_nr_ns(current, task_active_pid_ns(t));
	} else if (info == SEND_SIG_PRIV) {
		
		force = true;
	} else if (has_si_pid_and_uid(info)) {
		
		struct user_namespace *t_user_ns;

		rcu_read_lock();
		t_user_ns = task_cred_xxx(t, user_ns);
		if (current_user_ns() != t_user_ns) {
			kuid_t uid = make_kuid(current_user_ns(), info->si_uid);
			info->si_uid = from_kuid_munged(t_user_ns, uid);
		}
		rcu_read_unlock();

		
		force = (info->si_code == SI_KERNEL);

		
		if (!task_pid_nr_ns(current, task_active_pid_ns(t))) {
			info->si_pid = 0;
			force = true;
		}
	}
	return __send_signal_locked(sig, info, t, type, force);
}

static void print_fatal_signal(int signr)
{
	
}

static int __init setup_print_fatal_signals(char *str)
{
	get_option (&str, &print_fatal_signals);

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
	for (;;) {
		sighand = rcu_dereference(tsk->sighand);
		if (unlikely(sighand == NULL))
			break;

		
		spin_lock_irqsave(&sighand->siglock, *flags);
		if (likely(sighand == rcu_access_pointer(tsk->sighand)))
			break;
		spin_unlock_irqrestore(&sighand->siglock, *flags);
	}
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
	int error = -ESRCH;
	struct task_struct *p;

	for (;;) {
		rcu_read_lock();
		p = pid_task(pid, PIDTYPE_PID);
		if (p)
			error = group_send_sig_info(sig, info, p, PIDTYPE_TGID);
		rcu_read_unlock();
		if (likely(!p || error != -ESRCH))
			return error;

		
	}
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

int kill_pid_usb_asyncio(int sig, int errno, sigval_t addr,
			 struct pid *pid, const struct cred *cred)
{
	struct kernel_siginfo info;
	struct task_struct *p;
	unsigned long flags;
	int ret = -EINVAL;

	if (!valid_signal(sig))
		return ret;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = errno;
	info.si_code = SI_ASYNCIO;
	*((sigval_t *)&info.si_pid) = addr;

	rcu_read_lock();
	p = pid_task(pid, PIDTYPE_PID);
	if (!p) {
		ret = -ESRCH;
		goto out_unlock;
	}
	if (!kill_as_cred_perm(cred, p)) {
		ret = -EPERM;
		goto out_unlock;
	}
	ret = security_task_kill(p, &info, sig, cred);
	if (ret)
		goto out_unlock;

	if (sig) {
		if (lock_task_sighand(p, &flags)) {
			ret = __send_signal_locked(sig, &info, p, PIDTYPE_TGID, false);
			unlock_task_sighand(p, &flags);
		} else
			ret = -ESRCH;
	}
out_unlock:
	rcu_read_unlock();
	return ret;
}

static int kill_something_info(int sig, struct kernel_siginfo *info, pid_t pid)
{
	int ret;

	if (pid > 0)
		return kill_proc_info(sig, info, pid);

	
	if (pid == INT_MIN)
		return -ESRCH;

	read_lock(&tasklist_lock);
	if (pid != -1) {
		ret = __kill_pgrp_info(sig, info,
				pid ? find_vpid(-pid) : task_pgrp(current));
	} else {
		int retval = 0, count = 0;
		struct task_struct * p;

		for_each_process(p) {
			if (task_pid_vnr(p) > 1 &&
					!same_thread_group(p, current)) {
				int err = group_send_sig_info(sig, info, p,
							      PIDTYPE_MAX);
				++count;
				if (err != -EPERM)
					retval = err;
			}
		}
		ret = count ? retval : -ESRCH;
	}
	read_unlock(&tasklist_lock);

	return ret;
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

void force_sigsegv(int sig)
{
	if (sig == SIGSEGV)
		force_fatal_sig(SIGSEGV);
	else
		force_sig(SIGSEGV);
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

int force_sig_mceerr(int code, void __user *addr, short lsb)
{
	struct kernel_siginfo info;

	WARN_ON((code != BUS_MCEERR_AO) && (code != BUS_MCEERR_AR));
	clear_siginfo(&info);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = addr;
	info.si_addr_lsb = lsb;
	return force_sig_info(&info);
}

int send_sig_mceerr(int code, void __user *addr, short lsb, struct task_struct *t)
{
	struct kernel_siginfo info;

	WARN_ON((code != BUS_MCEERR_AO) && (code != BUS_MCEERR_AR));
	clear_siginfo(&info);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = addr;
	info.si_addr_lsb = lsb;
	return send_sig_info(info.si_signo, &info, t);
}

int force_sig_bnderr(void __user *addr, void __user *lower, void __user *upper)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code  = SEGV_BNDERR;
	info.si_addr  = addr;
	info.si_lower = lower;
	info.si_upper = upper;
	return force_sig_info(&info);
}

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

int send_sig_perf(void __user *addr, u32 type, u64 sig_data)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo     = SIGTRAP;
	info.si_errno     = 0;
	info.si_code      = TRAP_PERF;
	info.si_addr      = addr;
	info.si_perf_data = sig_data;
	info.si_perf_type = type;

	
	info.si_perf_flags = sigismember(&current->blocked, info.si_signo) ?
				     TRAP_PERF_FLAG_ASYNC :
				     0;

	return send_sig_info(info.si_signo, &info, current);
}

int force_sig_seccomp(int syscall, int reason, bool force_coredump)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = SIGSYS;
	info.si_code = SYS_SECCOMP;
	info.si_call_addr = (void __user *)KSTK_EIP(current);
	info.si_errno = reason;
	info.si_arch = syscall_get_arch(current);
	info.si_syscall = syscall;
	return force_sig_info_to_task(&info, current,
		force_coredump ? HANDLER_EXIT : HANDLER_CURRENT);
}

int force_sig_ptrace_errno_trap(int errno, void __user *addr)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = SIGTRAP;
	info.si_errno = errno;
	info.si_code  = TRAP_HWBKPT;
	info.si_addr  = addr;
	return force_sig_info(&info);
}

int force_sig_fault_trapno(int sig, int code, void __user *addr, int trapno)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = addr;
	info.si_trapno = trapno;
	return force_sig_info(&info);
}

int send_sig_fault_trapno(int sig, int code, void __user *addr, int trapno,
			  struct task_struct *t)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code  = code;
	info.si_addr  = addr;
	info.si_trapno = trapno;
	return send_sig_info(info.si_signo, &info, t);
}

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

struct sigqueue *sigqueue_alloc(void)
{
	return __sigqueue_alloc(-1, current, GFP_KERNEL, 0, SIGQUEUE_PREALLOC);
}

void sigqueue_free(struct sigqueue *q)
{
	unsigned long flags;
	spinlock_t *lock = &current->sighand->siglock;

	BUG_ON(!(q->flags & SIGQUEUE_PREALLOC));
	
	spin_lock_irqsave(lock, flags);
	q->flags &= ~SIGQUEUE_PREALLOC;
	
	if (!list_empty(&q->list))
		q = NULL;
	spin_unlock_irqrestore(lock, flags);

	if (q)
		__sigqueue_free(q);
}

int send_sigqueue(struct sigqueue *q, struct pid *pid, enum pid_type type)
{
	int sig = q->info.si_signo;
	struct sigpending *pending;
	struct task_struct *t;
	unsigned long flags;
	int ret, result;

	BUG_ON(!(q->flags & SIGQUEUE_PREALLOC));

	ret = -1;
	rcu_read_lock();
	t = pid_task(pid, type);
	if (!t || !likely(lock_task_sighand(t, &flags)))
		goto ret;

	ret = 1; 
	result = TRACE_SIGNAL_IGNORED;
	if (!prepare_signal(sig, t, false))
		goto out;

	ret = 0;
	if (unlikely(!list_empty(&q->list))) {
		
		BUG_ON(q->info.si_code != SI_TIMER);
		q->info.si_overrun++;
		result = TRACE_SIGNAL_ALREADY_PENDING;
		goto out;
	}
	q->info.si_overrun = 0;

	pending = (type != PIDTYPE_PID) ? &t->signal->shared_pending : &t->pending;
	list_add_tail(&q->list, &pending->list);
	sigaddset(&pending->signal, sig);
	complete_signal(sig, t, type);
	result = TRACE_SIGNAL_DELIVERED;
out:
	
	unlock_task_sighand(t, &flags);
ret:
	rcu_read_unlock();
	return ret;
}

static void do_notify_pidfd(struct task_struct *task)
{
	struct pid *pid;

	WARN_ON(task->exit_state == 0);
	pid = task_pid(task);
	wake_up_all(&pid->wait_pidfd);
}

bool do_notify_parent(struct task_struct *tsk, int sig)
{
	return false;
}

static void do_notify_parent_cldstop(struct task_struct *tsk,
				     bool for_ptracer, int why)
{
	struct kernel_siginfo info;
	unsigned long flags;
	struct task_struct *parent;
	struct sighand_struct *sighand;
	u64 utime, stime;

	if (for_ptracer) {
		parent = tsk->parent;
	} else {
		tsk = tsk->group_leader;
		parent = tsk->real_parent;
	}

	clear_siginfo(&info);
	info.si_signo = SIGCHLD;
	info.si_errno = 0;
	
	rcu_read_lock();
	info.si_pid = task_pid_nr_ns(tsk, task_active_pid_ns(parent));
	info.si_uid = from_kuid_munged(task_cred_xxx(parent, user_ns), task_uid(tsk));
	rcu_read_unlock();

	task_cputime(tsk, &utime, &stime);
	info.si_utime = nsec_to_clock_t(utime);
	info.si_stime = nsec_to_clock_t(stime);

 	info.si_code = why;
 	switch (why) {
 	case CLD_CONTINUED:
 		info.si_status = SIGCONT;
 		break;
 	case CLD_STOPPED:
 		info.si_status = tsk->signal->group_exit_code & 0x7f;
 		break;
 	case CLD_TRAPPED:
 		info.si_status = tsk->exit_code & 0x7f;
 		break;
 	default:
 		BUG();
 	}

	sighand = parent->sighand;
	spin_lock_irqsave(&sighand->siglock, flags);
	if (sighand->action[SIGCHLD-1].sa.sa_handler != SIG_IGN &&
	    !(sighand->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
		send_signal_locked(SIGCHLD, &info, parent, PIDTYPE_TGID);
	
	__wake_up_parent(tsk, parent);
	spin_unlock_irqrestore(&sighand->siglock, flags);
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

static bool do_signal_stop(int signr)
	__releases(&current->sighand->siglock)
{
	spin_unlock_irq(&current->sighand->siglock);
	return false;
}

static void do_jobctl_trap(void)
{
	/* Stub: no job control trap handling */
}

static void do_freezer_trap(void)
	__releases(&current->sighand->siglock)
{
	/* Stub: no freezer trap handling */
	spin_unlock_irq(&current->sighand->siglock);
}

static int ptrace_signal(int signr, kernel_siginfo_t *info, enum pid_type type)
{
	
	current->jobctl |= JOBCTL_STOP_DEQUEUED;
	signr = ptrace_stop(signr, CLD_TRAPPED, 0, info);

	
	if (signr == 0)
		return signr;

	
	if (signr != info->si_signo) {
		clear_siginfo(info);
		info->si_signo = signr;
		info->si_errno = 0;
		info->si_code = SI_USER;
		rcu_read_lock();
		info->si_pid = task_pid_vnr(current->parent);
		info->si_uid = from_kuid_munged(current_user_ns(),
						task_uid(current->parent));
		rcu_read_unlock();
	}

	
	if (sigismember(&current->blocked, signr) ||
	    fatal_signal_pending(current)) {
		send_signal_locked(signr, info, current, type);
		signr = 0;
	}

	return signr;
}

static void hide_si_addr_tag_bits(struct ksignal *ksig)
{
	switch (siginfo_layout(ksig->sig, ksig->info.si_code)) {
	case SIL_FAULT:
	case SIL_FAULT_TRAPNO:
	case SIL_FAULT_MCEERR:
	case SIL_FAULT_BNDERR:
	case SIL_FAULT_PKUERR:
	case SIL_FAULT_PERF_EVENT:
		ksig->info.si_addr = arch_untagged_si_addr(
			ksig->info.si_addr, ksig->sig, ksig->info.si_code);
		break;
	case SIL_KILL:
	case SIL_TIMER:
	case SIL_POLL:
	case SIL_CHLD:
	case SIL_RT:
	case SIL_SYS:
		break;
	}
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
	int group_stop = 0;
	sigset_t unblocked;

	
	cgroup_threadgroup_change_begin(tsk);

	if (thread_group_empty(tsk) || (tsk->signal->flags & SIGNAL_GROUP_EXIT)) {
		tsk->flags |= PF_EXITING;
		cgroup_threadgroup_change_end(tsk);
		return;
	}

	spin_lock_irq(&tsk->sighand->siglock);
	
	tsk->flags |= PF_EXITING;

	cgroup_threadgroup_change_end(tsk);

	if (!task_sigpending(tsk))
		goto out;

	unblocked = tsk->blocked;
	signotset(&unblocked);
	retarget_shared_pending(tsk, &unblocked);

	if (unlikely(tsk->jobctl & JOBCTL_STOP_PENDING) &&
	    task_participate_group_stop(tsk))
		group_stop = CLD_STOPPED;
out:
	spin_unlock_irq(&tsk->sighand->siglock);

	
	if (unlikely(group_stop)) {
		read_lock(&tasklist_lock);
		do_notify_parent_cldstop(tsk, false, group_stop);
		read_unlock(&tasklist_lock);
	}
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

static const struct {
	unsigned char limit, layout;
} sig_sicodes[] = {
	[SIGILL]  = { NSIGILL,  SIL_FAULT },
	[SIGFPE]  = { NSIGFPE,  SIL_FAULT },
	[SIGSEGV] = { NSIGSEGV, SIL_FAULT },
	[SIGBUS]  = { NSIGBUS,  SIL_FAULT },
	[SIGTRAP] = { NSIGTRAP, SIL_FAULT },
#if defined(SIGEMT)
	[SIGEMT]  = { NSIGEMT,  SIL_FAULT },
#endif
	[SIGCHLD] = { NSIGCHLD, SIL_CHLD },
	[SIGPOLL] = { NSIGPOLL, SIL_POLL },
	[SIGSYS]  = { NSIGSYS,  SIL_SYS },
};

static bool known_siginfo_layout(unsigned sig, int si_code)
{
	if (si_code == SI_KERNEL)
		return true;
	else if ((si_code > SI_USER)) {
		if (sig_specific_sicodes(sig)) {
			if (si_code <= sig_sicodes[sig].limit)
				return true;
		}
		else if (si_code <= NSIGPOLL)
			return true;
	}
	else if (si_code >= SI_DETHREAD)
		return true;
	else if (si_code == SI_ASYNCNL)
		return true;
	return false;
}

enum siginfo_layout siginfo_layout(unsigned sig, int si_code)
{
	/* Simplified layout determination for minimal system */
	if (si_code == SI_TIMER)
		return SIL_TIMER;
	else if (si_code == SI_SIGIO || si_code <= NSIGPOLL)
		return SIL_POLL;
	else if (si_code < 0)
		return SIL_RT;
	else if ((si_code > SI_USER) && (si_code < SI_KERNEL) &&
		 (sig < ARRAY_SIZE(sig_sicodes)) &&
		 (si_code <= sig_sicodes[sig].limit))
		return sig_sicodes[sig].layout;
	return SIL_KILL;
}

static inline char __user *si_expansion(const siginfo_t __user *info)
{
	return ((char __user *)info) + sizeof(struct kernel_siginfo);
}

int copy_siginfo_to_user(siginfo_t __user *to, const kernel_siginfo_t *from)
{
	char __user *expansion = si_expansion(to);
	if (copy_to_user(to, from , sizeof(struct kernel_siginfo)))
		return -EFAULT;
	if (clear_user(expansion, SI_EXPANSION_SIZE))
		return -EFAULT;
	return 0;
}

static int post_copy_siginfo_from_user(kernel_siginfo_t *info,
				       const siginfo_t __user *from)
{
	if (unlikely(!known_siginfo_layout(info->si_signo, info->si_code))) {
		char __user *expansion = si_expansion(from);
		char buf[SI_EXPANSION_SIZE];
		int i;
		
		if (copy_from_user(&buf, expansion, SI_EXPANSION_SIZE))
			return -EFAULT;
		for (i = 0; i < SI_EXPANSION_SIZE; i++) {
			if (buf[i] != 0)
				return -E2BIG;
		}
	}
	return 0;
}

static int __copy_siginfo_from_user(int signo, kernel_siginfo_t *to,
				    const siginfo_t __user *from)
{
	if (copy_from_user(to, from, sizeof(struct kernel_siginfo)))
		return -EFAULT;
	to->si_signo = signo;
	return post_copy_siginfo_from_user(to, from);
}

int copy_siginfo_from_user(kernel_siginfo_t *to, const siginfo_t __user *from)
{
	if (copy_from_user(to, from, sizeof(struct kernel_siginfo)))
		return -EFAULT;
	return post_copy_siginfo_from_user(to, from);
}

static int do_sigtimedwait(const sigset_t *which, kernel_siginfo_t *info,
		    const struct timespec64 *ts)
{
	ktime_t *to = NULL, timeout = KTIME_MAX;
	struct task_struct *tsk = current;
	sigset_t mask = *which;
	enum pid_type type;
	int sig, ret = 0;

	if (ts) {
		if (!timespec64_valid(ts))
			return -EINVAL;
		timeout = timespec64_to_ktime(*ts);
		to = &timeout;
	}

	
	sigdelsetmask(&mask, sigmask(SIGKILL) | sigmask(SIGSTOP));
	signotset(&mask);

	spin_lock_irq(&tsk->sighand->siglock);
	sig = dequeue_signal(tsk, &mask, info, &type);
	if (!sig && timeout) {
		
		tsk->real_blocked = tsk->blocked;
		sigandsets(&tsk->blocked, &tsk->blocked, &mask);
		recalc_sigpending();
		spin_unlock_irq(&tsk->sighand->siglock);

		__set_current_state(TASK_INTERRUPTIBLE);
		ret = freezable_schedule_hrtimeout_range(to, tsk->timer_slack_ns,
							 HRTIMER_MODE_REL);
		spin_lock_irq(&tsk->sighand->siglock);
		__set_task_blocked(tsk, &tsk->real_blocked);
		sigemptyset(&tsk->real_blocked);
		sig = dequeue_signal(tsk, &mask, info, &type);
	}
	spin_unlock_irq(&tsk->sighand->siglock);

	if (sig)
		return sig;
	return ret ? -EINTR : -EAGAIN;
}

SYSCALL_DEFINE4(rt_sigtimedwait, const sigset_t __user *, uthese,
		siginfo_t __user *, uinfo,
		const struct __kernel_timespec __user *, uts,
		size_t, sigsetsize)
{
	sigset_t these;
	struct timespec64 ts;
	kernel_siginfo_t info;
	int ret;

	
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&these, uthese, sizeof(these)))
		return -EFAULT;

	if (uts) {
		if (get_timespec64(&ts, uts))
			return -EFAULT;
	}

	ret = do_sigtimedwait(&these, &info, uts ? &ts : NULL);

	if (ret > 0 && uinfo) {
		if (copy_siginfo_to_user(uinfo, &info))
			ret = -EFAULT;
	}

	return ret;
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

static bool access_pidfd_pidns(struct pid *pid)
{
	struct pid_namespace *active = task_active_pid_ns(current);
	struct pid_namespace *p = ns_of_pid(pid);

	for (;;) {
		if (!p)
			return false;
		if (p == active)
			break;
		p = p->parent;
	}

	return true;
}

static int copy_siginfo_from_user_any(kernel_siginfo_t *kinfo,
		siginfo_t __user *info)
{
	return copy_siginfo_from_user(kinfo, info);
}

static struct pid *pidfd_to_pid(const struct file *file)
{
	struct pid *pid;

	pid = pidfd_pid(file);
	if (!IS_ERR(pid))
		return pid;

	return tgid_pidfd_to_pid(file);
}

SYSCALL_DEFINE4(pidfd_send_signal, int, pidfd, int, sig,
		siginfo_t __user *, info, unsigned int, flags)
{
	int ret;
	struct fd f;
	struct pid *pid;
	kernel_siginfo_t kinfo;

	
	if (flags)
		return -EINVAL;

	f = fdget(pidfd);
	if (!f.file)
		return -EBADF;

	
	pid = pidfd_to_pid(f.file);
	if (IS_ERR(pid)) {
		ret = PTR_ERR(pid);
		goto err;
	}

	ret = -EINVAL;
	if (!access_pidfd_pidns(pid))
		goto err;

	if (info) {
		ret = copy_siginfo_from_user_any(&kinfo, info);
		if (unlikely(ret))
			goto err;

		ret = -EINVAL;
		if (unlikely(sig != kinfo.si_signo))
			goto err;

		
		ret = -EPERM;
		if ((task_pid(current) != pid) &&
		    (kinfo.si_code >= 0 || kinfo.si_code == SI_TKILL))
			goto err;
	} else {
		prepare_kill_siginfo(sig, &kinfo);
	}

	ret = kill_pid_info(sig, &kinfo, pid);

err:
	fdput(f);
	return ret;
}

static int
do_send_specific(pid_t tgid, pid_t pid, int sig, struct kernel_siginfo *info)
{
	struct task_struct *p;
	int error = -ESRCH;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (p && (tgid <= 0 || task_tgid_vnr(p) == tgid)) {
		error = check_kill_permission(sig, info, p);
		
		if (!error && sig) {
			error = do_send_sig_info(sig, info, p, PIDTYPE_PID);
			
			if (unlikely(error == -ESRCH))
				error = 0;
		}
	}
	rcu_read_unlock();

	return error;
}

static int do_tkill(pid_t tgid, pid_t pid, int sig)
{
	struct kernel_siginfo info;

	clear_siginfo(&info);
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_TKILL;
	info.si_pid = task_tgid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	return do_send_specific(tgid, pid, sig, &info);
}

SYSCALL_DEFINE3(tgkill, pid_t, tgid, pid_t, pid, int, sig)
{
	
	if (pid <= 0 || tgid <= 0)
		return -EINVAL;

	return do_tkill(tgid, pid, sig);
}

SYSCALL_DEFINE2(tkill, pid_t, pid, int, sig)
{
	
	if (pid <= 0)
		return -EINVAL;

	return do_tkill(0, pid, sig);
}

static int do_rt_sigqueueinfo(pid_t pid, int sig, kernel_siginfo_t *info)
{
	
	if ((info->si_code >= 0 || info->si_code == SI_TKILL) &&
	    (task_pid_vnr(current) != pid))
		return -EPERM;

	
	return kill_proc_info(sig, info, pid);
}

SYSCALL_DEFINE3(rt_sigqueueinfo, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	kernel_siginfo_t info;
	int ret = __copy_siginfo_from_user(sig, &info, uinfo);
	if (unlikely(ret))
		return ret;
	return do_rt_sigqueueinfo(pid, sig, &info);
}

static int do_rt_tgsigqueueinfo(pid_t tgid, pid_t pid, int sig, kernel_siginfo_t *info)
{
	
	if (pid <= 0 || tgid <= 0)
		return -EINVAL;

	
	if ((info->si_code >= 0 || info->si_code == SI_TKILL) &&
	    (task_pid_vnr(current) != pid))
		return -EPERM;

	return do_send_specific(tgid, pid, sig, info);
}

SYSCALL_DEFINE4(rt_tgsigqueueinfo, pid_t, tgid, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	kernel_siginfo_t info;
	int ret = __copy_siginfo_from_user(sig, &info, uinfo);
	if (unlikely(ret))
		return ret;
	return do_rt_tgsigqueueinfo(tgid, pid, sig, &info);
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
	struct task_struct *p = current, *t;
	struct k_sigaction *k;
	sigset_t mask;

	if (!valid_signal(sig) || sig < 1 || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &p->sighand->action[sig-1];

	spin_lock_irq(&p->sighand->siglock);
	if (k->sa.sa_flags & SA_IMMUTABLE) {
		spin_unlock_irq(&p->sighand->siglock);
		return -EINVAL;
	}
	if (oact)
		*oact = *k;

	
	BUILD_BUG_ON(UAPI_SA_FLAGS & SA_UNSUPPORTED);

	
	if (act)
		act->sa.sa_flags &= UAPI_SA_FLAGS;
	if (oact)
		oact->sa.sa_flags &= UAPI_SA_FLAGS;

	sigaction_compat_abi(act, oact);

	if (act) {
		sigdelsetmask(&act->sa.sa_mask,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
		*k = *act;
		
		if (sig_handler_ignored(sig_handler(p, sig), sig)) {
			sigemptyset(&mask);
			sigaddset(&mask, sig);
			flush_sigqueue_mask(&mask, &p->signal->shared_pending);
			for_each_thread(p, t)
				flush_sigqueue_mask(&mask, &t->pending);
		}
	}

	spin_unlock_irq(&p->sighand->siglock);
	return 0;
}

static inline void sigaltstack_lock(void)
	__acquires(&current->sighand->siglock)
{
	spin_lock_irq(&current->sighand->siglock);
}

static inline void sigaltstack_unlock(void)
	__releases(&current->sighand->siglock)
{
	spin_unlock_irq(&current->sighand->siglock);
}

static int
do_sigaltstack (const stack_t *ss, stack_t *oss, unsigned long sp,
		size_t min_ss_size)
{
	struct task_struct *t = current;
	int ret = 0;

	if (oss) {
		memset(oss, 0, sizeof(stack_t));
		oss->ss_sp = (void __user *) t->sas_ss_sp;
		oss->ss_size = t->sas_ss_size;
		oss->ss_flags = sas_ss_flags(sp) |
			(current->sas_ss_flags & SS_FLAG_BITS);
	}

	if (ss) {
		void __user *ss_sp = ss->ss_sp;
		size_t ss_size = ss->ss_size;
		unsigned ss_flags = ss->ss_flags;
		int ss_mode;

		if (unlikely(on_sig_stack(sp)))
			return -EPERM;

		ss_mode = ss_flags & ~SS_FLAG_BITS;
		if (unlikely(ss_mode != SS_DISABLE && ss_mode != SS_ONSTACK &&
				ss_mode != 0))
			return -EINVAL;

		
		if (t->sas_ss_sp == (unsigned long)ss_sp &&
		    t->sas_ss_size == ss_size &&
		    t->sas_ss_flags == ss_flags)
			return 0;

		sigaltstack_lock();
		if (ss_mode == SS_DISABLE) {
			ss_size = 0;
			ss_sp = NULL;
		} else {
			if (unlikely(ss_size < min_ss_size))
				ret = -ENOMEM;
			if (!sigaltstack_size_valid(ss_size))
				ret = -ENOMEM;
		}
		if (!ret) {
			t->sas_ss_sp = (unsigned long) ss_sp;
			t->sas_ss_size = ss_size;
			t->sas_ss_flags = ss_flags;
		}
		sigaltstack_unlock();
	}
	return ret;
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

