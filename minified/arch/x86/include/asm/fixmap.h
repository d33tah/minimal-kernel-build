 
#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#define KM_MAX_IDX 16

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <linux/threads.h>

extern unsigned long __FIXADDR_TOP;
#define FIXADDR_TOP	((unsigned long)__FIXADDR_TOP)

enum fixed_addresses {
	FIX_HOLE,
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_MAX_IDX * NR_CPUS) - 1,

	__end_of_permanent_fixed_addresses,

#define NR_FIX_BTMAPS		64
#define FIX_BTMAPS_SLOTS	8
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)
	FIX_BTMAP_END =
	 (__end_of_permanent_fixed_addresses ^
	  (__end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS - 1)) &
	 -PTRS_PER_PTE
	 ? __end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS -
	   (__end_of_permanent_fixed_addresses & (TOTAL_FIX_BTMAPS - 1))
	 : __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	__end_of_fixed_addresses
};

#define FIXADDR_TOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_TOT_START	(FIXADDR_TOP - FIXADDR_TOT_SIZE)

#include <linux/bug.h>
#include <linux/mm_types.h>

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))

static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	BUILD_BUG_ON(idx >= __end_of_fixed_addresses);
	return __fix_to_virt(idx);
}

#endif
#endif  
