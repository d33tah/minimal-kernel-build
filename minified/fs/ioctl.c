#include <linux/syscalls.h>

SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
{
	return -ENOTTY;
}
