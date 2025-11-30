// Stubbed new mount API syscalls
// Original: 172 LOC
// Not needed for minimal boot - we use simple static mounts

#include <linux/syscalls.h>
#include <linux/errno.h>

// Stub: fsopen - new mount API not needed
SYSCALL_DEFINE2(fsopen, const char __user *, _fs_name, unsigned int, flags)
{
	return -ENOSYS;
}

// Stub: fspick - new mount API not needed  
SYSCALL_DEFINE3(fspick, int, dfd, const char __user *, path, unsigned int, flags)
{
	return -ENOSYS;
}

// Stub: fsconfig - new mount API not needed
SYSCALL_DEFINE5(fsconfig, int, fd, unsigned int, cmd, const char __user *, _key,
		const void __user *, _value, int, aux)
{
	return -ENOSYS;
}
