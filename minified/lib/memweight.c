// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/bitmap.h>

/**
 * memweight - count the total number of bits set in memory area - STUBBED
 */
size_t memweight(const void *ptr, size_t bytes)
{
	return 0;
}
EXPORT_SYMBOL(memweight);
