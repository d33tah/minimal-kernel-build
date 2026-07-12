
#include <linux/sched/mm.h>
DECLARE_PER_CPU(unsigned long, process_counts);
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/task_work.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/fs_struct.h>


static void delayed_put_task_struct(struct rcu_head *rhp) {
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);


	put_task_struct(tsk);
}

void put_task_struct_rcu_user(struct task_struct *task) {
	if (refcount_dec_and_test(&task->rcu_users))
		call_rcu(&task->rcu, delayed_put_task_struct);
}

static void exit_mm(void) {
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: exit_mm drops the dying task's mm. On this
	 * single-shot boot the only task that exits is PID-1 init, and do_exit
	 * panics ("Attempted to kill init!") on the is_global_init() branch
	 * BEFORE it reaches exit_mm (trace: do_exit entered then je->panic; the
	 * whole exit tail exit_mm/exit_files/exit_fs/... is HIT=False). No-op is
	 * safe -- the system never tears down an mm on a 1-shot boot. The private
	 * wrapper exit_mm_release (fork.c, sole caller here) was deleted; its
	 * mm_release body lives on via the still-present exec_mm_release.
	 */
}

static void exit_notify(struct task_struct *tsk, int group_dead) {
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

void __noreturn do_exit(long code) {
	struct task_struct *tsk = current;
	int group_dead;

	exit_signals(tsk);

	if (tsk->mm)
		sync_mm_rss(tsk->mm);
	group_dead = atomic_dec_and_test(&tsk->signal->live);
	if (group_dead) {
		
		if (unlikely(is_global_init(tsk)))
			panic("Attempted to kill init! exitcode=0x%08x\n", (int)code);
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

SYSCALL_DEFINE1(exit, int, error_code) {
	do_exit((error_code&0xff)<<8);
}

/*
 * Removed: do_group_exit + SYSCALL_DEFINE1(exit_group) - unreachable. The init
 * ELF exits via exit(2) (__NR_exit), never exit_group(2); the syscall_32.tbl
 * entry 252 is gone (routes to sys_ni). Cascade-orphaned zap_other_threads /
 * signal_wake_up / signal_wake_up_state were removed from signal.c too.
 */


