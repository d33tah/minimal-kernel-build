
#include <linux/sched/mm.h>
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/fs_struct.h>

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

void __noreturn do_exit(long code)
{
	current->flags |= PF_EXITING;

	/*
	 * init (PID 1) is the only user task (callers: exit(2) and fatal user
	 * page faults), so every exit is init exiting, which is fatal.
	 */
	panic("Attempted to kill init! exitcode=0x%08x\n", (int)code);
}

SYSCALL_DEFINE1(exit, int, error_code)
{
	do_exit((error_code & 0xff) << 8);
}

/*
 * Removed: do_group_exit + SYSCALL_DEFINE1(exit_group) - unreachable. The init
 * ELF exits via exit(2) (__NR_exit), never exit_group(2); the syscall_32.tbl
 * entry 252 is gone (routes to sys_ni). Cascade-orphaned zap_other_threads /
 * signal_wake_up / signal_wake_up_state were removed from signal.c too.
 */
