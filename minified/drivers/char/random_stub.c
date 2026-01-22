/* Stub random number generator */
#include <linux/syscalls.h>
/* get_random_bytes, random_fops, urandom_fops removed - never called */
SYSCALL_DEFINE3(getrandom, char __user *, buf, size_t, count, unsigned int,
		flags)
{
	return count;
}
