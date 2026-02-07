
#include <linux/mm.h>
/* linux/slab.h removed - no slab functions */
#include <linux/sched/mm.h>
extern int nr_threads;
/* end sched/stat.h */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
/* linux/interrupt.h, linux/capability.h, linux/completion.h, linux/personality.h removed - unused */
#include <linux/tty.h>
/* linux/cpu.h removed - no cpu features used */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/binfmts.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
/* linux/mount.h removed - vfsmount not used */
/* proc_fs.h removed - empty header */
/* mempolicy.h removed - forward decl in mm.h */
/* linux/cgroup.h removed - no cgroup features used */
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/resource.h>
#include <linux/task_work.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
/* linux/writeback.h removed - no writeback features used */
#include <linux/rcuwait.h>
/* linux/compat.h removed - no compat features used */

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/mmu_context.h>

/* ptrace stubs (merged from ptrace.c) */
/* __ptrace_unlink removed - empty stub, call site removed */
/* exit_ptrace removed - empty stub */

/* release_task, delayed_put_task_struct, put_task_struct_rcu_user removed -
 * only called from do_exit which is now a panic stub.
 * Hello World kernel never exits processes. */

void put_task_struct_rcu_user(struct task_struct *task)
{
	/* Stub - never called during Hello World boot */
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

/* do_exit gutted - Hello World kernel never exits processes.
 * init loops forever, kthreadd loops forever, no kthreads created.
 * Only reachable via make_task_dead (fatal errors). */
void __noreturn do_exit(long code)
{
	panic("do_exit called with code %ld\n", code);
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

/* exit_group syscall removed - not in syscall table */

/* wait syscalls replaced with COND_SYSCALL */
