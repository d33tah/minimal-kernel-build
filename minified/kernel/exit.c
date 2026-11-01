#include <linux/sched/task.h>
#include <linux/syscalls.h>

void __noreturn do_exit(long code)
{
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
