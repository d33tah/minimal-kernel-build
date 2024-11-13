// SPDX-License-Identifier: GPL-2.0
#include <linux/memblock.h>
#include <linux/mmdebug.h>
#include <linux/export.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <linux/vmalloc.h>

#include "physaddr.h"

#ifdef CONFIG_X86_64


bool __virt_addr_valid(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	if (unlikely(x > y)) {
		x = y + phys_base;

		if (y >= KERNEL_IMAGE_SIZE)
			return false;
	} else {
		x = y + (__START_KERNEL_map - PAGE_OFFSET);

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		if ((x > y) || !phys_addr_valid(x))
			return false;
	}

	return pfn_valid(x >> PAGE_SHIFT);
}
EXPORT_SYMBOL(__virt_addr_valid);

#else


bool __virt_addr_valid(unsigned long x)
{
	if (x < PAGE_OFFSET)
		return false;
	if (__vmalloc_start_set && is_vmalloc_addr((void *) x))
		return false;
	if (x >= FIXADDR_START)
		return false;
	return pfn_valid((x - PAGE_OFFSET) >> PAGE_SHIFT);
}
EXPORT_SYMBOL(__virt_addr_valid);

#endif	/* CONFIG_X86_64 */
