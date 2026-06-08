// SPDX-License-Identifier: GPL-2.0
// Stubbed out TLS syscalls - not needed for "Hello World"
#include <linux/syscalls.h>
#include <linux/regset.h>

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
