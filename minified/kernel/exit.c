
#include <linux/sched/mm.h>
extern int nr_threads;
DECLARE_PER_CPU(unsigned long, process_counts);
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/task_work.h>
#include <linux/user_namespace.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/fs_struct.h>

#include <asm/unistd.h>
#include <asm/mmu_context.h>

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
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: release_task reaps a dead task_struct, but
	 * nothing is ever reaped on this build -- the only exiting tasks are
	 * PID-1 init (never reaped: it panics if it exits) and kthreads (which
	 * never exit). The reparent/zombie loops in exit_notify/find_child_reaper
	 * that call this are themselves runtime-dead. No-op is safe; the private
	 * teardown subtree (__exit_signal/__unhash_process/flush_sigqueue/
	 * __sigqueue_free/detach_pid/__change_pid) was deleted.
	 */
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


