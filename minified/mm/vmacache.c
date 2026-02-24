#include <linux/mm.h>
#include <linux/vmacache.h>

void vmacache_update(unsigned long addr, struct vm_area_struct *newvma)
{
}

struct vm_area_struct *vmacache_find(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}
