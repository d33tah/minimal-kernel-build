 

#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#include <asm/kmap_size.h>


# define FIXMAP_PMD_NUM	2
/* FIXMAP_PMD_TOP removed - never used */

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/pgtable_types.h>
#include <linux/threads.h>

 
 
extern unsigned long __FIXADDR_TOP;
#define FIXADDR_TOP	((unsigned long)__FIXADDR_TOP)

 
enum fixed_addresses {
	FIX_HOLE,
	/* FIX_DBGP_BASE, FIX_EARLYCON_MEM_BASE removed - never used */
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
	FIX_WP_TEST,
	__end_of_fixed_addresses
};


/* FIXADDR_SIZE, FIXADDR_START removed - unused */
#define FIXADDR_TOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_TOT_START	(FIXADDR_TOP - FIXADDR_TOT_SIZE)

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte);
void native_set_fixmap(unsigned   idx,
		       phys_addr_t phys, pgprot_t flags);

static inline void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	native_set_fixmap(idx, phys, flags);
}

 
#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_IO_NOCACHE

/* early_memremap_{encrypted,decrypted}[_wp] declarations removed - never called */

/* Inlined from asm-generic/fixmap.h */
#include <linux/bug.h>
#include <linux/mm_types.h>

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))
/* __virt_to_fix removed - unused */

static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	BUILD_BUG_ON(idx >= __end_of_fixed_addresses);
	return __fix_to_virt(idx);
}


#ifndef FIXMAP_PAGE_NORMAL
#define FIXMAP_PAGE_NORMAL PAGE_KERNEL
#endif
/* FIXMAP_PAGE_RO removed - unused */
#ifndef FIXMAP_PAGE_NOCACHE
#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_NOCACHE
#endif
#ifndef FIXMAP_PAGE_IO
#define FIXMAP_PAGE_IO PAGE_KERNEL_IO
#endif
#ifndef FIXMAP_PAGE_CLEAR
#define FIXMAP_PAGE_CLEAR __pgprot(0)
#endif

#ifndef set_fixmap
#define set_fixmap(idx, phys)				\
	__set_fixmap(idx, phys, FIXMAP_PAGE_NORMAL)
#endif

#ifndef clear_fixmap
#define clear_fixmap(idx)			\
	__set_fixmap(idx, 0, FIXMAP_PAGE_CLEAR)
#endif

#define __set_fixmap_offset(idx, phys, flags)				\
({									\
	unsigned long ________addr;					\
	__set_fixmap(idx, phys, flags);					\
	________addr = fix_to_virt(idx) + ((phys) & (PAGE_SIZE - 1));	\
	________addr;							\
})

/* set_fixmap_offset, set_fixmap_nocache, set_fixmap_offset_nocache,
   set_fixmap_io, set_fixmap_offset_io removed - unused */

#define __late_set_fixmap(idx, phys, flags) __set_fixmap(idx, phys, flags)
#define __late_clear_fixmap(idx) __set_fixmap(idx, 0, __pgprot(0))

void __early_set_fixmap(enum fixed_addresses idx,
			phys_addr_t phys, pgprot_t flags);

#endif  
#endif  
