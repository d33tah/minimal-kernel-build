
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
extern int nr_threads;
DECLARE_PER_CPU(unsigned long, process_counts);
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
#include <linux/cgroup.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/posix-timers.h>
#include <linux/mutex.h>
#include <linux/pipe_fs_i.h>
#include <linux/resource.h>
#include <linux/blkdev.h>
#include <linux/task_work.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
#include <linux/perf_event.h>
#include <linux/oom.h>
#include <linux/writeback.h>
#include <linux/random.h>
#include <linux/compat.h>
#include <linux/kprobes.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/mmu_context.h>

static void __unhash_process(struct task_struct *p, bool group_dead)
{
	nr_threads--;
	detach_pid(p, PIDTYPE_PID);
	if (group_dead) {
		detach_pid(p, PIDTYPE_TGID);
		detach_pid(p, PIDTYPE_PGID);
		detach_pid(p, PIDTYPE_SID);

		list_del_rcu(&p->tasks);
		__this_cpu_dec(process_counts);
	}
	list_del_rcu(&p->thread_group);
	list_del_rcu(&p->thread_node);
}

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
	}

	__unhash_process(tsk, group_dead);

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

	/*
	 * do_notify_parent() is a permanent `return false;` stub on this build,
	 * so the leader-zombie reap arm and the zap_leader/goto-repeat loop are
	 * dead; release_task always processes exactly one task.
	 */
	write_unlock_irq(&tasklist_lock);
	proc_flush_pid(thread_pid);
	put_pid(thread_pid);
	release_thread(p);
	put_task_struct_rcu_user(p);
}



static void exit_mm(void)
{
	struct mm_struct *mm = current->mm;

	exit_mm_release(current, mm);
	if (!mm)
		return;
	sync_mm_rss(mm);
	mmap_read_lock(mm);
	mmgrab(mm);
	BUG_ON(mm != current->active_mm);
	
	task_lock(current);
	
	smp_mb__after_spinlock();
	local_irq_disable();
	current->mm = NULL;
	enter_lazy_tlb(mm, current);
	local_irq_enable();
	task_unlock(current);
	mmap_read_unlock(mm);
	mmput(mm);
}

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
	__releases(&tasklist_lock)
	__acquires(&tasklist_lock)
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

static void forget_original_parent(struct task_struct *father,
					struct list_head *dead)
{
	/*
	 * The child-reparenting walk is runtime-dead on this build: the only
	 * exiting tasks are PID-1 init (which forks nothing -- it just execs the
	 * static init ELF that does write(2)+exit) and individual kthreads, none
	 * of which ever have children. kthreadd (the sole parent of kthreads)
	 * runs an infinite loop and never exits. No task ever has children here,
	 * so find_child_reaper()'s early return is the only live path; the
	 * reparent loop, find_new_reaper() and reparent_leader() were statically
	 * reachable but never executed. (The dead task_struct.children/.sibling
	 * list was removed since nothing ever iterated it.)
	 */
	find_child_reaper(father, dead);
}

static void exit_notify(struct task_struct *tsk, int group_dead)
{
	bool autoreap;
	struct task_struct *p, *n;
	LIST_HEAD(dead);

	write_lock_irq(&tasklist_lock);
	forget_original_parent(tsk, &dead);

	tsk->exit_state = EXIT_ZOMBIE;
	/*
	 * do_notify_parent() is a permanent `return false;` stub, so a
	 * thread-group leader never auto-reaps; only a non-leader does.
	 */
	autoreap = !thread_group_leader(tsk);

	if (autoreap) {
		tsk->exit_state = EXIT_DEAD;
		list_add(&tsk->ptrace_entry, &dead);
	}

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

	exit_signals(tsk);

	if (tsk->mm)
		sync_mm_rss(tsk->mm);
	group_dead = atomic_dec_and_test(&tsk->signal->live);
	if (group_dead) {
		
		if (unlikely(is_global_init(tsk)))
			panic("Attempted to kill init! exitcode=0x%08x\n",
				(int)code);
	}

	exit_mm();



	exit_files(tsk);
	exit_fs(tsk);
	exit_task_namespaces(tsk);
	exit_task_work(tsk);
	exit_thread(tsk);

	/* sched_autogroup_exit_task - stubbed */

	exit_notify(tsk, group_dead);

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

SYSCALL_DEFINE1(exit, int, error_code)
{
	do_exit((error_code&0xff)<<8);
}

/*
 * Removed: do_group_exit + SYSCALL_DEFINE1(exit_group) - unreachable. The init
 * ELF exits via exit(2) (__NR_exit), never exit_group(2); the syscall_32.tbl
 * entry 252 is gone (routes to sys_ni). Cascade-orphaned zap_other_threads /
 * signal_wake_up / signal_wake_up_state were removed from signal.c too.
 */


