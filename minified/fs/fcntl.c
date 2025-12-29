/* Stub fcntl - minimal file control */
#include <linux/fs.h>
#include <linux/syscalls.h>

/* __f_setown, kill_fasync removed - empty stubs */

int fasync_helper(int fd, struct file *filp, int on,
		  struct fasync_struct **fapp)
{
	return 0;
}
SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
{
	return -EINVAL;
}
SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd, unsigned long,
		arg)
{
	return -EINVAL;
}
