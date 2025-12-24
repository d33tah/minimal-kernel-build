/* Stubbed new mount API syscalls - not needed for minimal boot */
#include <linux/syscalls.h>
#include <linux/errno.h>
SYSCALL_DEFINE2(fsopen, const char __user *, _fs_name, unsigned int, flags)
{
	return -ENOSYS;
}
SYSCALL_DEFINE3(fspick, int, dfd, const char __user *, path, unsigned int,
		flags)
{
	return -ENOSYS;
}
SYSCALL_DEFINE5(fsconfig, int, fd, unsigned int, cmd, const char __user *, _key,
		const void __user *, _value, int, aux)
{
	return -ENOSYS;
}
