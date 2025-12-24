/* Minimal includes for mprotect stubs */
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <asm/tlb.h>
#include "internal.h"
int mprotect_fixup(struct mmu_gather *tlb, struct vm_area_struct *vma,
		   struct vm_area_struct **pprev, unsigned long start,
		   unsigned long end, unsigned long newflags)
{
	*pprev = vma;
	return 0;
}
SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long,
		prot)
{
	return 0;
}
