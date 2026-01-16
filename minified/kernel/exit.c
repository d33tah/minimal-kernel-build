
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
extern int nr_threads;
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/cpu.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/binfmts.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/mempolicy.h>
#include <linux/cgroup.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/posix-timers.h>
#include <linux/mutex.h>
#include <linux/futex.h>
#include <linux/pipe_fs_i.h>
#include <linux/resource.h>
#include <linux/blkdev.h>
#include <linux/task_work.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
#include <linux/writeback.h>
#include <linux/rcuwait.h>
#include <linux/compat.h>
#include <linux/kprobes.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/mmu_context.h>

static void __exit_signal(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	bool group_dead = thread_group_leader(tsk);
	struct sighand_struct *sighand;
	struct tty_struct *tty;

	sighand = rcu_dereference_check(tsk->sighand,
					lockdep_tasklist_lock_is_held());
	spin_lock(&sighand->siglock);

	if (group_dead) {
		tty = sig->tty;
		sig->tty = NULL;
	} else {
		if (sig->notify_count > 0 && !--sig->notify_count)
			wake_up_process(sig->group_exec_task);

		if (tsk == sig->curr_target)
			sig->curr_target = next_thread(tsk);
	}

	/* add_device_randomness, task_cputime, sig->utime/stime, min_flt/maj_flt/nvcsw/nivcsw,
	 * sum_sched_runtime, io accounting all removed - write-only or empty stub fields
	 * stats_lock also removed since it protected those removed fields */
	sig->nr_threads--;
	nr_threads--;
	detach_pid(tsk, PIDTYPE_PID);
	if (group_dead) {
		detach_pid(tsk, PIDTYPE_TGID);
		detach_pid(tsk, PIDTYPE_PGID);
		detach_pid(tsk, PIDTYPE_SID);
		list_del_rcu(&tsk->tasks);
		list_del_init(&tsk->sibling);
	}
	list_del_rcu(&tsk->thread_group);
	list_del_rcu(&tsk->thread_node);

	flush_sigqueue(&tsk->pending);
	tsk->sighand = NULL;
	spin_unlock(&sighand->siglock);

	__cleanup_sighand(sighand);
	clear_tsk_thread_flag(tsk, TIF_SIGPENDING);
	if (group_dead) {
		flush_sigqueue(&sig->shared_pending);
		tty_kref_put(tty);
	}
}

static void delayed_put_task_struct(struct rcu_head *rhp)
{
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);

	put_task_struct(tsk);
}

void put_task_struct_rcu_user(struct task_struct *task)
{
	if (refcount_dec_and_test(&task->rcu_users))
		call_rcu(&task->rcu, delayed_put_task_struct);
}

void release_task(struct task_struct *p)
{
	struct pid *thread_pid;

	rcu_read_lock();
	dec_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	rcu_read_unlock();

	write_lock_irq(&tasklist_lock);
	ptrace_release_task(p);
	thread_pid = get_pid(p->thread_pid);
	__exit_signal(p);

	/* do_notify_parent always returns false, so zap_leader logic removed */

	write_unlock_irq(&tasklist_lock);
	put_pid(thread_pid);
	release_thread(p);
	put_task_struct_rcu_user(p);
}

int rcuwait_wake_up(struct rcuwait *w)
{
	int ret = 0;
	struct task_struct *task;

	rcu_read_lock();

	smp_mb();

	task = rcu_dereference(w->task);
	if (task)
		ret = wake_up_process(task);
	rcu_read_unlock();

	return ret;
}

/* coredump_task_exit inlined into do_exit */

static struct task_struct *find_alive_thread(struct task_struct *p)
{
	struct task_struct *t;

	for_each_thread(p, t) {
		if (!(t->flags & PF_EXITING))
			return t;
	}
	return NULL;
}

static struct task_struct *find_child_reaper(struct task_struct *father,
					     struct list_head *dead)
	__releases(&tasklist_lock) __acquires(&tasklist_lock)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(father);
	struct task_struct *reaper = pid_ns->child_reaper;
	struct task_struct *p, *n;

	if (likely(reaper != father))
		return reaper;

	reaper = find_alive_thread(father);
	if (reaper) {
		pid_ns->child_reaper = reaper;
		return reaper;
	}

	write_unlock_irq(&tasklist_lock);

	list_for_each_entry_safe(p, n, dead, ptrace_entry) {
		list_del_init(&p->ptrace_entry);
		release_task(p);
	}

	zap_pid_ns_processes(pid_ns);
	write_lock_irq(&tasklist_lock);

	return father;
}

static struct task_struct *find_new_reaper(struct task_struct *father,
					   struct task_struct *child_reaper)
{
	struct task_struct *thread, *reaper;

	thread = find_alive_thread(father);
	if (thread)
		return thread;

	if (father->signal->has_child_subreaper) {
		unsigned int ns_level = task_pid(father)->level;

		for (reaper = father->real_parent;
		     task_pid(reaper)->level == ns_level;
		     reaper = reaper->real_parent) {
			if (reaper == &init_task)
				break;
			if (!reaper->signal->is_child_subreaper)
				continue;
			thread = find_alive_thread(reaper);
			if (thread)
				return thread;
		}
	}

	return child_reaper;
}

/* reparent_leader inlined into forget_original_parent */

static void forget_original_parent(struct task_struct *father,
				   struct list_head *dead)
{
	struct task_struct *p, *t, *reaper;

	if (unlikely(!list_empty(&father->ptraced)))
		exit_ptrace(father, dead);

	reaper = find_child_reaper(father, dead);
	if (list_empty(&father->children))
		return;

	reaper = find_new_reaper(father, reaper);
	list_for_each_entry(p, &father->children, sibling) {
		for_each_thread(p, t) {
			RCU_INIT_POINTER(t->real_parent, reaper);
			BUG_ON((!t->ptrace) !=
			       (rcu_access_pointer(t->parent) == father));
			if (likely(!t->ptrace))
				t->parent = t->real_parent;
			if (t->pdeath_signal)
				group_send_sig_info(t->pdeath_signal,
						    SEND_SIG_NOINFO, t,
						    PIDTYPE_TGID);
		}

		/* Inlined reparent_leader */
		if (!same_thread_group(reaper, father) &&
		    likely(p->exit_state != EXIT_DEAD)) {
			p->exit_signal = SIGCHLD;
			/* do_notify_parent always returns false - dead code removed */
		}
	}
	list_splice_tail_init(&father->children, &reaper->children);
}

static void exit_notify(struct task_struct *tsk, int group_dead)
{
	bool autoreap;
	struct task_struct *p, *n;
	LIST_HEAD(dead);

	write_lock_irq(&tasklist_lock);
	forget_original_parent(tsk, &dead);

	tsk->exit_state = EXIT_ZOMBIE;
	/* do_notify_parent always returns false, so simplified:
	 * autoreap = !tsk->ptrace && !thread_group_leader(tsk) */
	autoreap = !tsk->ptrace && !thread_group_leader(tsk);

	if (autoreap) {
		tsk->exit_state = EXIT_DEAD;
		list_add(&tsk->ptrace_entry, &dead);
	}

	if (unlikely(tsk->signal->notify_count < 0))
		wake_up_process(tsk->signal->group_exec_task);
	write_unlock_irq(&tasklist_lock);

	list_for_each_entry_safe(p, n, &dead, ptrace_entry) {
		list_del_init(&p->ptrace_entry);
		release_task(p);
	}
}

void __noreturn do_exit(long code)
{
	struct task_struct *tsk = current;
	int group_dead;

	/* coredump_task_exit removed - PF_POSTCOREDUMP never tested */
	ptrace_event(PTRACE_EVENT_EXIT, code);

	/* validate_creds_for_do_exit(), io_uring_files_cancel() - empty stubs */
	exit_signals(tsk);

	group_dead = atomic_dec_and_test(&tsk->signal->live);
	if (group_dead) {
		if (unlikely(is_global_init(tsk)))
			panic("Attempted to kill init! exitcode=0x%08x\n",
			      tsk->signal->group_exit_code ?: (int)code);
	}
	tsk->exit_code = code;

	/* Inlined exit_mm */
	{
		struct mm_struct *mm = current->mm;
		exit_mm_release(current, mm);
		if (mm) {
			mmap_read_lock(mm);
			mmgrab(mm);
			BUG_ON(mm != current->active_mm);
			task_lock(current);
			local_irq_disable();
			current->mm = NULL;
			enter_lazy_tlb(mm, current);
			local_irq_enable();
			task_unlock(current);
			mmap_read_unlock(mm);
			mmput(mm);
		}
	}
	exit_files(tsk);
	exit_fs(tsk);
	exit_task_namespaces(tsk);
	exit_task_work(tsk);
	exit_thread(tsk);
	exit_notify(tsk, group_dead);

	/* validate_creds_for_do_exit call removed - empty stub */
	exit_task_stack_account(tsk);
	preempt_disable();

	do_task_dead();
}

void __noreturn make_task_dead(int signr)
{
	struct task_struct *tsk = current;

	if (unlikely(in_interrupt()))
		panic("Aiee, killing interrupt handler!");
	if (unlikely(!tsk->pid))
		panic("Attempted to kill the idle task!");

	if (unlikely(in_atomic())) {
		preempt_count_set(PREEMPT_ENABLED);
	}

	if (unlikely(tsk->flags & PF_EXITING)) {
		pr_alert("Fixing recursive fault but reboot is needed!\n");
		tsk->exit_state = EXIT_DEAD;
		refcount_inc(&tsk->rcu_users);
		do_task_dead();
	}

	do_exit(signr);
}

/* Stub: exit syscalls - Hello World doesn't need to exit cleanly */
SYSCALL_DEFINE1(exit, int, error_code)
{
	return 0;
}

SYSCALL_DEFINE1(exit_group, int, error_code)
{
	return 0;
}

/* wait syscalls replaced with COND_SYSCALL */
