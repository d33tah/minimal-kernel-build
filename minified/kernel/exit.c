
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

static void exit_notify(struct task_struct *tsk, int group_dead)
{
	/*
	 * RUNTIME-DEAD anchor-stub: exit_notify is the process-reap/reparent
	 * notification root. On this single-shot boot nothing is ever reaped --
	 * release_task is already a no-op (#8), init/kthreadd never exit, and no
	 * task ever has children to reparent. do_exit reaches its entry but
	 * never gets here (trace HIT=False). Its whole private reap subtree --
	 * forget_original_parent -> find_child_reaper -> find_alive_thread (and
	 * the zap_pid_ns_processes/release_task tail) -- was deleted; an empty
	 * no-op is correct because the task is torn down via do_task_dead anyway
	 * and a never-reaped zombie can't matter on a system that never reaps.
	 */
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


