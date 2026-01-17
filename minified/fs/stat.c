#include <linux/syscalls.h>

/* generic_fillattr, vfs_getattr removed - stub callers simplified */

/* readlink returns -EINVAL not -ENOSYS, can't use COND_SYSCALL */
SYSCALL_DEFINE4(readlinkat, int, dfd, const char __user *, pathname,
		char __user *, buf, int, bufsiz)
{
	return -EINVAL;
}
SYSCALL_DEFINE3(readlink, const char __user *, path, char __user *, buf, int,
		bufsiz)
{
	return -EINVAL;
}
