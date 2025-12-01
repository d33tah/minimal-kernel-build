
#include <linux/capability.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/uio.h>
#include <linux/audit.h>
#include <linux/pid_namespace.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/regset.h>
#include <linux/hw_breakpoint.h>
#include <linux/cn_proc.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>
#include <linux/minmax.h>

#include <asm/syscall.h>

int ptrace_access_vm(struct task_struct *tsk, unsigned long addr,
		     void *buf, int len, unsigned int gup_flags)
{
	return -EIO;
}

void __ptrace_link(struct task_struct *child, struct task_struct *new_parent,
		   const struct cred *ptracer_cred)
{
}

void __ptrace_unlink(struct task_struct *child)
{
}

bool ptrace_may_access(struct task_struct *task, unsigned int mode)
{
	return false;
}

void exit_ptrace(struct task_struct *tracer, struct list_head *dead)
{
}

int ptrace_readdata(struct task_struct *tsk, unsigned long src, char __user *dst, int len)
{
	return -EIO;
}

int ptrace_writedata(struct task_struct *tsk, char __user *src, unsigned long dst, int len)
{
	return -EIO;
}

int ptrace_request(struct task_struct *child, long request,
		   unsigned long addr, unsigned long data)
{
	return -EIO;
}

SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
		unsigned long, data)
{
	return -ENOSYS;
}

int generic_ptrace_peekdata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data)
{
	return -EIO;
}

int generic_ptrace_pokedata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data)
{
	return -EIO;
}

int ptrace_get_breakpoints(struct task_struct *tsk)
{
	return 0;
}

void ptrace_put_breakpoints(struct task_struct *tsk)
{
}

int task_current_syscall(struct task_struct *target, struct syscall_info *info)
{
	return -ENOSYS;
}
