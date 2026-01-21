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
/* seccomp.h removed - header is empty */

/* PTRACE_EVENT_FORK, VFORK, CLONE, VFORK_DONE removed - never used */
#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENT_EXIT	6
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2


/* ptrace_access_vm removed - declared but never implemented */


#define PT_SEIZED	0x00010000	 
#define PT_PTRACED	0x00000001

#define PT_OPT_FLAG_SHIFT	3
#define PT_EVENT_FLAG(event)	(1 << (PT_OPT_FLAG_SHIFT + (event)))
#define PT_TRACESYSGOOD		PT_EVENT_FLAG(0)

/* arch_ptrace, ptrace_disable removed - never called */
/* ptrace_request removed - never called */
extern int ptrace_notify(int exit_code, unsigned long message);
extern void __ptrace_link(struct task_struct *child,
			  struct task_struct *new_parent,
			  const struct cred *ptracer_cred);
extern void __ptrace_unlink(struct task_struct *child);
/* exit_ptrace removed - empty stub never effectively called */

/* PTRACE_MODE_*, ptrace_may_access, ptrace_reparented removed - never called */

static inline void ptrace_unlink(struct task_struct *child)
{
	if (unlikely(child->ptrace))
		__ptrace_unlink(child);
}



static inline bool ptrace_event_enabled(struct task_struct *task, int event)
{
	return task->ptrace & PT_EVENT_FLAG(event);
}

static inline void ptrace_event(int event, unsigned long message)
{
	if (unlikely(ptrace_event_enabled(current, event))) {
		ptrace_notify((event << 8) | SIGTRAP, message);
	} else if (event == PTRACE_EVENT_EXEC) {
		 
		if ((current->ptrace & (PT_PTRACED|PT_SEIZED)) == PT_PTRACED)
			send_sig(SIGTRAP, current, 0);
	}
}

/* ptrace_event_pid removed - never called */

static inline void ptrace_init_task(struct task_struct *child, bool ptrace)
{
	INIT_LIST_HEAD(&child->ptrace_entry);
	INIT_LIST_HEAD(&child->ptraced);
	child->jobctl = 0;
	child->ptrace = 0;
	child->parent = child->real_parent;

	if (unlikely(ptrace) && current->ptrace) {
		child->ptrace = current->ptrace;
		__ptrace_link(child, current->parent, NULL);  /* ptracer_cred field removed */

		/* task_set_jobctl_pending removed - always returns false */
		sigaddset(&child->pending.signal, SIGSTOP);
	}
	/* child->ptracer_cred = NULL removed - field removed from task_struct */
}

static inline void ptrace_release_task(struct task_struct *task)
{
	BUG_ON(!list_empty(&task->ptraced));
	ptrace_unlink(task);
	BUG_ON(!list_empty(&task->ptrace_entry));
}
/* force_successful_syscall_return, is_syscall_success removed - never called */

/* x86 defines arch_has_single_step, arch_has_block_step,
 * and ARCH_HAS_USER_SINGLE_STEP_REPORT */
/* user_enable_single_step, user_enable_block_step,
   user_disable_single_step, arch_ptrace_stop_needed, arch_ptrace_stop
   removed - declared but never called */
extern void user_single_step_report(struct pt_regs *regs);

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

#ifndef signal_pt_regs
#define signal_pt_regs() task_pt_regs(current)
#endif

/* current_user_stack_pointer removed - never used */


static inline int ptrace_report_syscall(unsigned long message)
{
	int ptrace = current->ptrace;
	int signr;

	if (!(ptrace & PT_PTRACED))
		return 0;

	signr = ptrace_notify(SIGTRAP | ((ptrace & PT_TRACESYSGOOD) ? 0x80 : 0),
			      message);

	 
	if (signr)
		send_sig(signr, current, 1);

	return fatal_signal_pending(current);
}

static inline __must_check int ptrace_report_syscall_entry(
	struct pt_regs *regs)
{
	return ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_ENTRY);
}

static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step)
{
	if (step)
		user_single_step_report(regs);
	else
		ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_EXIT);
}
#endif
