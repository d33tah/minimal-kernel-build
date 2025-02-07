// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/mm.h>

#include <asm/page.h>

#include "physaddr.h"
#include "asm/fixmap.h"
#include "asm/page_32.h"
#include "asm/page_types.h"
#include "asm/pgtable_32_areas.h"
#include "linux/stddef.h"



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

