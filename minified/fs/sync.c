#include <linux/syscalls.h>

SYSCALL_DEFINE0(sync)
{
	return 0;
}
