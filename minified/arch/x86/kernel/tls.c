// set_thread_area/get_thread_area syscalls replaced with COND_SYSCALL
// regset_tls_* removed - only used from user_x86_32_view which was removed
#include <linux/syscalls.h>

int do_set_thread_area(struct task_struct *p, int idx,
		       struct user_desc __user *u_info, int can_allocate)
{
	return -ENOSYS;
}
