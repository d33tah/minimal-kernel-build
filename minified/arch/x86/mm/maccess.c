// SPDX-License-Identifier: GPL-2.0-only

#include <linux/uaccess.h>
#include <asm/page_types.h>

#include "linux/types.h"

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	return (unsigned long)unsafe_src >= TASK_SIZE_MAX;
}
