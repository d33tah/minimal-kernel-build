/* Minimal includes - all functions are stubs */
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/errno.h>

SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
		unsigned long, data)
{
	return -ENOSYS;
}

