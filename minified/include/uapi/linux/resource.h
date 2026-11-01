/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_RESOURCE_H
#define _UAPI_LINUX_RESOURCE_H

struct rlimit {
	__kernel_ulong_t rlim_cur;
	__kernel_ulong_t rlim_max;
};

#define _STK_LIM (8 * 1024 * 1024)
#define MLOCK_LIMIT (8 * 1024 * 1024)

#include <asm/resource.h>

#endif /* _UAPI_LINUX_RESOURCE_H */
