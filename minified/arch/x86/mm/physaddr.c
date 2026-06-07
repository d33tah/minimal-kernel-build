/* Minimal includes for physaddr */
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
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

