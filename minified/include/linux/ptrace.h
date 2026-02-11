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
/* PTRACE_EVENT_FORK, VFORK, CLONE, VFORK_DONE, EXEC, EXIT removed - never used */
/* PTRACE_EVENTMSG_SYSCALL_ENTRY, PTRACE_EVENTMSG_SYSCALL_EXIT removed - never used */


/* ptrace_access_vm removed - declared but never implemented */



#define PT_OPT_FLAG_SHIFT	3

/* arch_ptrace, ptrace_disable removed - never called */
/* ptrace_request removed - never called */
/* ptrace_notify removed - no callers */
/* __ptrace_link removed - declared but never called */
/* __ptrace_unlink removed - empty stub */
/* exit_ptrace removed - empty stub never effectively called */

/* PTRACE_MODE_*, ptrace_may_access, ptrace_reparented removed - never called */

/* ptrace_unlink removed - __ptrace_unlink was empty */



/* ptrace_event_enabled, ptrace_event stubbed - ptrace is always 0 */
static inline void ptrace_event(int event, unsigned long message)
{
}

/* ptrace_event_pid removed - never called */

/* ptrace_init_task inlined at single call site in fork.c */

/* ptrace_release_task removed - never called, ptrace fields removed */
/* force_successful_syscall_return, is_syscall_success removed - never called */

/* x86 defines arch_has_single_step, arch_has_block_step,
 * and ARCH_HAS_USER_SINGLE_STEP_REPORT */
/* user_enable_single_step, user_enable_block_step,
   user_disable_single_step, arch_ptrace_stop_needed, arch_ptrace_stop
   removed - declared but never called */
/* user_single_step_report removed - ptrace_report_syscall_exit stubbed */

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

#ifndef signal_pt_regs
#define signal_pt_regs() task_pt_regs(current)
#endif

/* current_user_stack_pointer removed - never used */


/* ptrace_report_syscall* stubbed - ptrace is always 0 */
static inline __must_check int ptrace_report_syscall_entry(
	struct pt_regs *regs)
{
	return 0;
}

static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step)
{
}
#endif
