// SPDX-License-Identifier: GPL-2.0
// set_thread_area/get_thread_area syscalls replaced with COND_SYSCALL
#include <linux/regset.h>

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
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	return -ENOSYS;
}

int regset_tls_active(struct task_struct *target,
		      const struct user_regset *regset)
{
	return 0;
}
