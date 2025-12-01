#include <linux/memblock.h>
#include <linux/mmdebug.h>
#include <linux/export.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <linux/vmalloc.h>

#include "physaddr.h"



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

