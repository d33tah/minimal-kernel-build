
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/syscalls.h>

void __noreturn do_exit(long code)
{
	current->flags |= PF_EXITING;

	/*
	 * init (PID 1) is the only user task (callers: exit(2), fatal user page
	 * faults and user-mode CPU exceptions), so every exit is init exiting,
	 * which is fatal.
	 */
	panic("Attempted to kill init! exitcode=0x%08x\n", (int)code);
}

void __noreturn make_task_dead(int signr)
{
	/*
	 * Take the task off the cpu after something catastrophic has
	 * happened.
	 */
	struct task_struct *tsk = current;

	if (unlikely(in_interrupt()))
		panic("Aiee, killing interrupt handler!");
	if (unlikely(!tsk->pid))
		panic("Attempted to kill the idle task!");

	do_exit(signr);
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
