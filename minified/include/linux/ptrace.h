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

#define PTRACE_EVENT_FORK	1
#define PTRACE_EVENT_VFORK	2
#define PTRACE_EVENT_CLONE	3
#define PTRACE_EVENT_EXEC	4
#define PTRACE_EVENT_VFORK_DONE	5
#define PTRACE_EVENT_EXIT	6
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2


extern int ptrace_access_vm(struct task_struct *tsk, unsigned long addr,
			    void *buf, int len, unsigned int gup_flags);


#define PT_SEIZED	0x00010000	 
#define PT_PTRACED	0x00000001

#define PT_OPT_FLAG_SHIFT	3
#define PT_EVENT_FLAG(event)	(1 << (PT_OPT_FLAG_SHIFT + (event)))
#define PT_TRACESYSGOOD		PT_EVENT_FLAG(0)

#define PTRACE_MODE_READ	0x01
#define PTRACE_MODE_ATTACH	0x02
#define PTRACE_MODE_NOAUDIT	0x04
#define PTRACE_MODE_FSCREDS	0x08
#define PTRACE_MODE_REALCREDS	0x10



static inline void ptrace_unlink(struct task_struct *child)
{
}



static inline bool ptrace_event_enabled(struct task_struct *task, int event)
{
	/* task->ptrace is never set in this minimal boot (no ptrace(2)). */
	return false;
}

static inline void ptrace_event(int event, unsigned long message)
{
}

static inline void ptrace_event_pid(int event, struct pid *pid)
{
}

static inline void ptrace_init_task(struct task_struct *child, bool ptrace)
{
	INIT_LIST_HEAD(&child->ptrace_entry);
	INIT_LIST_HEAD(&child->ptraced);
	child->jobctl = 0;
	child->ptrace = 0;
	child->parent = child->real_parent;

	child->ptracer_cred = NULL;
}

static inline void ptrace_release_task(struct task_struct *task)
{
	BUG_ON(!list_empty(&task->ptraced));
	ptrace_unlink(task);
	BUG_ON(!list_empty(&task->ptrace_entry));
}

#ifndef force_successful_syscall_return
#define force_successful_syscall_return() do { } while (0)
#endif

#ifndef is_syscall_success
#define is_syscall_success(regs) (!IS_ERR_VALUE((unsigned long)(regs_return_value(regs))))
#endif


#ifndef arch_has_single_step
#define arch_has_single_step()		(0)

static inline void user_enable_single_step(struct task_struct *task)
{
	BUG();			 
}

static inline void user_disable_single_step(struct task_struct *task)
{
}
#else
extern void user_enable_single_step(struct task_struct *);
extern void user_disable_single_step(struct task_struct *);
#endif	 

#ifndef arch_has_block_step
#define arch_has_block_step()		(0)

static inline void user_enable_block_step(struct task_struct *task)
{
	BUG();			 
}
#else
extern void user_enable_block_step(struct task_struct *);
#endif	 

#ifdef ARCH_HAS_USER_SINGLE_STEP_REPORT
extern void user_single_step_report(struct pt_regs *regs);
#else
static inline void user_single_step_report(struct pt_regs *regs)
{
	kernel_siginfo_t info;
	clear_siginfo(&info);
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info(&info);
}
#endif

#ifndef arch_ptrace_stop_needed
#define arch_ptrace_stop_needed()	(0)
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
	if (step)
		user_single_step_report(regs);
	else
		ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_EXIT);
}
#endif
