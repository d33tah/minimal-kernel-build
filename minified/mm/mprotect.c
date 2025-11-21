// Stubbed memory protection change implementation
// Original: 658 LOC
// Not needed for minimal boot - hello world doesn't change memory protection

#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <asm/tlb.h>

#include "internal.h"

// Stub: change_protection - pretend we changed protection
unsigned long change_protection(struct mmu_gather *tlb,
			      struct vm_area_struct *vma, unsigned long start,
			      unsigned long end, pgprot_t newprot,
			      unsigned long cp_flags)
{
	// Return 0 pages changed
	return 0;
}

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
