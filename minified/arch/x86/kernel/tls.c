// SPDX-License-Identifier: GPL-2.0
// Stubbed out TLS syscalls - not needed for "Hello World"

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/regset.h>
#include <asm/desc.h>

SYSCALL_DEFINE1(set_thread_area, void __user *, u_info)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(get_thread_area, void __user *, u_info)
{
	return -ENOSYS;
}

int do_get_thread_area(struct task_struct *p, int idx,
			struct user_desc __user *u_info)
{
	return -ENOSYS;
}

int do_set_thread_area(struct task_struct *p, int idx,
			struct user_desc __user *u_info, int can_allocate)
{
	return -ENOSYS;
}

int regset_tls_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	return -ENOSYS;
}

int regset_tls_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	return -ENOSYS;
}

int regset_tls_active(struct task_struct *target, const struct user_regset *regset)
{
	return 0;
}
