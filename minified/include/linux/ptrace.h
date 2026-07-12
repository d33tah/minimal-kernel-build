#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/compiler.h>		 
#include <linux/sched.h>		 
#include <linux/sched/signal.h>
#include <linux/pid_namespace.h>	 
#include <asm/ptrace.h>

#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2


/* ptrace_unlink: empty stub orphaned with ptrace_release_task, removed (LOC reduction) */



static inline void ptrace_event(int event, unsigned long message) { }

static inline void ptrace_init_task(struct task_struct *child, bool ptrace) {
	INIT_LIST_HEAD(&child->ptrace_entry);
	child->jobctl = 0;
	child->parent = child->real_parent; }

/* ptrace_release_task: 0-caller static-inline orphan removed (LOC reduction) */

#ifndef arch_has_single_step

static inline void user_disable_single_step(struct task_struct *task) { }
#endif


#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif



static inline int ptrace_report_syscall(unsigned long message) {
	/* task->ptrace is never set (no ptrace(2)), so this is a no-op. */
	return 0; }

static inline __must_check int ptrace_report_syscall_entry( struct pt_regs *regs) {
	return ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_ENTRY); }

static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step) {
	ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_EXIT); }
#endif
