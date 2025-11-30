 
 

#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/thread_info.h>	 
#include <asm/unistd.h>

typedef long (*sys_call_ptr_t)(const struct pt_regs *);
extern const sys_call_ptr_t sys_call_table[];

#define ia32_sys_call_table sys_call_table

 
static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->orig_ax;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->ax = regs->orig_ax;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ax;
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->ax;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->ax = (long) error ?: val;
}


static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, &regs->bx, 6 * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_I386;
}


#endif	 
