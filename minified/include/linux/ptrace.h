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

static inline void ptrace_event(int event, unsigned long message)
{
}

/* ptrace_init_task inlined at single call site in fork.c */

/* x86 defines arch_has_single_step, arch_has_block_step,
 * and ARCH_HAS_USER_SINGLE_STEP_REPORT */
/* user_enable_single_step, user_enable_block_step,
   user_disable_single_step, arch_ptrace_stop_needed, arch_ptrace_stop
   removed - declared but never called */

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

#ifndef signal_pt_regs
#define signal_pt_regs() task_pt_regs(current)
#endif

static inline __must_check int ptrace_report_syscall_entry(
	struct pt_regs *regs)
{
	return 0;
}

static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step)
{
}
#endif
