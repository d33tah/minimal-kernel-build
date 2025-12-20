/* Minimal includes for mprotect stubs */
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <asm/tlb.h>

#include "internal.h"

// Stub: mprotect_fixup - called by setup_arg_pages during exec
int mprotect_fixup(struct mmu_gather *tlb, struct vm_area_struct *vma,
		struct vm_area_struct **pprev, unsigned long start,
		unsigned long end, unsigned long newflags)
{
	// For hello world, just update the prev pointer and return success
	*pprev = vma;
	return 0;
}

// Stub: mprotect syscall - return success without doing anything
SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
		unsigned long, prot)
{
	// For hello world, we don't need to change memory protection
	// Just return success
	return 0;
}
