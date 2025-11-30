 

#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#include <asm/kmap_size.h>

 
# define FIXMAP_PMD_NUM	2
 
#define FIXMAP_PMD_TOP	507

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
	FIX_DBGP_BASE,
	FIX_EARLYCON_MEM_BASE,
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


extern void reserve_top_address(unsigned long reserve);

#define FIXADDR_SIZE		(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)
#define FIXADDR_TOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_TOT_START	(FIXADDR_TOP - FIXADDR_TOT_SIZE)

extern int fixmaps_set;

extern pte_t *pkmap_page_table;

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte);
void native_set_fixmap(unsigned   idx,
		       phys_addr_t phys, pgprot_t flags);

static inline void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	native_set_fixmap(idx, phys, flags);
}

 
#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_IO_NOCACHE

 
void __init *early_memremap_encrypted(resource_size_t phys_addr,
				      unsigned long size);
void __init *early_memremap_encrypted_wp(resource_size_t phys_addr,
					 unsigned long size);
void __init *early_memremap_decrypted(resource_size_t phys_addr,
				      unsigned long size);
void __init *early_memremap_decrypted_wp(resource_size_t phys_addr,
					 unsigned long size);

#include <asm-generic/fixmap.h>

#define __late_set_fixmap(idx, phys, flags) __set_fixmap(idx, phys, flags)
#define __late_clear_fixmap(idx) __set_fixmap(idx, 0, __pgprot(0))

void __early_set_fixmap(enum fixed_addresses idx,
			phys_addr_t phys, pgprot_t flags);

#endif  
#endif  
