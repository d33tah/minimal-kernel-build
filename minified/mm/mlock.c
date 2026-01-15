/* mprotect stub - returns 0 (success) instead of -ENOSYS */
#include <linux/syscalls.h>

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long,
		prot)
{
	return 0;
}
