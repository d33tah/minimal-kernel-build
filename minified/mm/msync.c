// SPDX-License-Identifier: GPL-2.0
/*
 * msync() syscall - Stubbed minimal implementation
 */
#include <linux/syscalls.h>

/* Stubbed msync - not needed for minimal boot */
SYSCALL_DEFINE3(msync, unsigned long, start, size_t, len, int, flags)
{
	return 0;
}
