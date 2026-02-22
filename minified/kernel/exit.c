#include <linux/fdtable.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/task_work.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/mmu_context.h>

/* ptrace stubs (merged from ptrace.c) */

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

void __noreturn make_task_dead(int signr)
{
	panic("make_task_dead(%d)\n", signr);
}

/* do_exit gutted - Hello World kernel never exits processes.
 * init loops forever, kthreadd loops forever, no kthreads created.
 * Only reachable via make_task_dead (fatal errors). */
void __noreturn do_exit(long code)
{
	panic("do_exit called with code %ld\n", code);
}

/* Stub: exit syscalls - Hello World doesn't need to exit cleanly */
SYSCALL_DEFINE1(exit, int, error_code)
{
	return 0;
}

/* wait syscalls replaced with COND_SYSCALL */
