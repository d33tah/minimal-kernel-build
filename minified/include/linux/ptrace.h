#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/compiler.h>		 
#include <linux/sched.h>		 
#include <linux/sched/signal.h>		 
#include <linux/err.h>			 
#include <linux/bug.h>			 
#include <linux/pid_namespace.h>	 
#include <linux/types.h>
#include <asm/ptrace.h>

#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2


static inline void ptrace_unlink(struct task_struct *child)
{
}



static inline void ptrace_event(int event, unsigned long message)
{
}

static inline void ptrace_init_task(struct task_struct *child, bool ptrace)
{
	INIT_LIST_HEAD(&child->ptrace_entry);
	child->jobctl = 0;
	child->parent = child->real_parent;
}

static inline void ptrace_release_task(struct task_struct *task)
{
	ptrace_unlink(task);
	BUG_ON(!list_empty(&task->ptrace_entry));
}

#ifndef force_successful_syscall_return
#define force_successful_syscall_return() do { } while (0)
#endif


#ifndef arch_has_single_step

static inline void user_disable_single_step(struct task_struct *task)
{
}
#endif

#ifndef arch_ptrace_stop
#define arch_ptrace_stop()		do { } while (0)
#endif

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

#ifndef signal_pt_regs
#define signal_pt_regs() task_pt_regs(current)
#endif

#ifndef current_user_stack_pointer
#define current_user_stack_pointer() user_stack_pointer(current_pt_regs())
#endif


static inline int ptrace_report_syscall(unsigned long message)
{
	/* task->ptrace is never set (no ptrace(2)), so this is a no-op. */
	return 0;
}

static inline __must_check int ptrace_report_syscall_entry(
	struct pt_regs *regs)
{
	return ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_ENTRY);
}

static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step)
{
	ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_EXIT);
}
#endif
