/* Minimal includes - all functions are stubs */
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/errno.h>

void __ptrace_link(struct task_struct *child, struct task_struct *new_parent,
		   const struct cred *ptracer_cred)
{
}

void __ptrace_unlink(struct task_struct *child)
{
}

void exit_ptrace(struct task_struct *tracer, struct list_head *dead)
{
}

int ptrace_request(struct task_struct *child, long request, unsigned long addr,
		   unsigned long data)
{
	return -EIO;
}

SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
		unsigned long, data)
{
	return -ENOSYS;
}
