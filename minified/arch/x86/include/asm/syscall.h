/* --- 2025-12-06 13:10 --- elf-em.h replaced with elf.h */
#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <linux/elf.h>
#include <linux/sched.h>

/* __AUDIT_ARCH_LE, AUDIT_ARCH_I386 removed - never used */
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

/* syscall_rollback removed - never called */

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ax;
	return IS_ERR_VALUE(error) ? error : 0;
}

/* syscall_get_arch removed - never called */

#endif	 
