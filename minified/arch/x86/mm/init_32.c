// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#include <linux/memblock.h>

#include <linux/swap.h>
#include <asm/cpufeature.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#include <asm/pgtable_areas.h>

#include "mm_internal.h"

static void paging_init(void);

/*
 * Creates a middle page table and puts a pointer to it in the
 * given global directory entry. This only returns the gd entry
 * in non-PAE compilation mode, since the middle layer is folded.
 */
static pmd_t *__init one_md_table_init(pgd_t *pgd)
{
	return pmd_offset(pud_offset(p4d_offset(pgd, 0), 0), 0);
}

/*
 * Create a page table and place a pointer to it in a middle page
 * directory entry:
 */
static pte_t *__init one_page_table_init(pmd_t *pmd)
{
	if (!(pmd_val(*pmd) & _PAGE_PRESENT)) {
		pte_t *page_table = (pte_t *)alloc_low_page();

		set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_TABLE));
		BUG_ON(page_table != pte_offset_kernel(pmd, 0));
	}

	return pte_offset_kernel(pmd, 0);
}

static void __init page_table_range_init(unsigned long start, unsigned long end,
					 pgd_t *pgd_base)
{
	int pgd_idx, pmd_idx;
	unsigned long vaddr;
	pgd_t *pgd;
	pmd_t *pmd;

	vaddr = start;
	pgd_idx = pgd_index(vaddr);
	pmd_idx = pmd_index(vaddr);
	pgd = pgd_base + pgd_idx;

	for (; (pgd_idx < PTRS_PER_PGD) && (vaddr != end); pgd++, pgd_idx++) {
		pmd = one_md_table_init(pgd);
		pmd = pmd + pmd_index(vaddr);
		for (; (pmd_idx < PTRS_PER_PMD) && (vaddr != end);
		     pmd++, pmd_idx++) {
			one_page_table_init(pmd);

			vaddr += PMD_SIZE;
		}
		pmd_idx = 0;
	}
}

static inline int is_x86_32_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_text && addr <= (unsigned long)__init_end)
		return 1;
	return 0;
}

unsigned long __init kernel_physical_mapping_init(unsigned long start,
						  unsigned long end,
						  unsigned long page_size_mask,
						  pgprot_t prot)
{
	int use_pse = page_size_mask == (1 << PG_LEVEL_2M);
	unsigned long last_map_addr = end;
	unsigned long start_pfn, end_pfn;
	pgd_t *pgd_base = swapper_pg_dir;
	int pgd_idx, pmd_idx, pte_ofs;
	unsigned long pfn;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int mapping_iter;

	start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	/*
	 * First iteration will setup identity mapping using large/small pages
	 * based on use_pse, with other attributes same as set by
	 * the early code in head_32.S
	 *
	 * Second iteration will setup the appropriate attributes (NX, GLOBAL..)
	 * as desired for the kernel identity mapping.
	 *
	 * This two pass mechanism conforms to the TLB app note which says:
	 *
	 *     "Software should not write to a paging-structure entry in a way
	 *      that would change, for any linear address, both the page size
	 *      and either the page frame or attributes."
	 */
	mapping_iter = 1;

	if (!boot_cpu_has(X86_FEATURE_PSE))
		use_pse = 0;

repeat:
	pfn = start_pfn;
	pgd_idx = pgd_index((pfn << PAGE_SHIFT) + PAGE_OFFSET);
	pgd = pgd_base + pgd_idx;
	for (; pgd_idx < PTRS_PER_PGD; pgd++, pgd_idx++) {
		pmd = one_md_table_init(pgd);

		if (pfn >= end_pfn)
			continue;
		pmd_idx = 0;
		for (; pmd_idx < PTRS_PER_PMD && pfn < end_pfn;
		     pmd++, pmd_idx++) {
			unsigned int addr = pfn * PAGE_SIZE + PAGE_OFFSET;

			/*
			 * Map with big pages if possible, otherwise
			 * create normal page tables:
			 */
			if (use_pse) {
				unsigned int addr2;
				pgprot_t prot = PAGE_KERNEL_LARGE;

				/*
				 * first pass will use the same initial
				 * identity mapping attribute + _PAGE_PSE.
				 */
				pgprot_t init_prot =
					__pgprot(PTE_IDENT_ATTR | _PAGE_PSE);

				pfn &= PMD_MASK >> PAGE_SHIFT;
				addr2 = (pfn + PTRS_PER_PTE - 1) * PAGE_SIZE +
					PAGE_OFFSET + PAGE_SIZE - 1;

				if (is_x86_32_kernel_text(addr) ||
				    is_x86_32_kernel_text(addr2))
					prot = PAGE_KERNEL_LARGE_EXEC;

				if (mapping_iter == 1)
					set_pmd(pmd, pfn_pmd(pfn, init_prot));
				else
					set_pmd(pmd, pfn_pmd(pfn, prot));

				pfn += PTRS_PER_PTE;
				continue;
			}
			pte = one_page_table_init(pmd);

			pte_ofs = pte_index((pfn << PAGE_SHIFT) + PAGE_OFFSET);
			pte += pte_ofs;
			for (; pte_ofs < PTRS_PER_PTE && pfn < end_pfn;
			     pte++, pfn++, pte_ofs++, addr += PAGE_SIZE) {
				pgprot_t prot = PAGE_KERNEL;

				/*
				 * first pass will use the same initial
				 * identity mapping attribute.
				 */
				pgprot_t init_prot = __pgprot(PTE_IDENT_ATTR);

				if (is_x86_32_kernel_text(addr))
					prot = PAGE_KERNEL_EXEC;

				if (mapping_iter == 1) {
					set_pte(pte, pfn_pte(pfn, init_prot));
					last_map_addr =
						(pfn << PAGE_SHIFT) + PAGE_SIZE;
				} else
					set_pte(pte, pfn_pte(pfn, prot));
			}
		}
	}
	if (mapping_iter == 1) {
		/*
		 * local global flush tlb, which will flush the previous
		 * mappings present in both small and large page TLB's.
		 */
		__flush_tlb_all();

		/*
		 * Second iteration will set the actual desired PTE attributes.
		 */
		mapping_iter = 2;
		goto repeat;
	}
	return last_map_addr;
}

void __init native_pagetable_init(void)
{
	unsigned long pfn, va;
	pgd_t *pgd, *base = swapper_pg_dir;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * Remove any mappings which extend past the end of physical
	 * memory from the boot time page table.
	 * In virtual address space, we should have at least two pages
	 * from VMALLOC_END to pkmap or fixmap according to VMALLOC_END
	 * definition. And max_low_pfn is set to VMALLOC_END physical
	 * address. If initial memory mapping is doing right job, we
	 * should have pte used near max_low_pfn or one pmd is not present.
	 */
	for (pfn = max_low_pfn; pfn < 1 << (32 - PAGE_SHIFT); pfn++) {
		va = PAGE_OFFSET + (pfn << PAGE_SHIFT);
		pgd = base + pgd_index(va);

		pmd = pmd_offset(pud_offset(p4d_offset(pgd, va), va), va);
		if (!pmd_present(*pmd))
			break;

		/* should not be large page here */
		if (pmd_large(*pmd)) {
			BUG_ON(1);
		}

		pte = pte_offset_kernel(pmd, va);
		if (!pte_present(*pte))
			break;

		pte_clear(NULL, va, pte);
	}
	paging_init();
}

/*
 * Build a proper pagetable for the kernel mappings.  Up until this
 * point, we've been running on some set of pagetables constructed by
 * the boot process.
 *
 * If we're booting on native hardware, this will be a pagetable
 * constructed in arch/x86/kernel/head_32.S.  The root of the
 * pagetable will be swapper_pg_dir.
 *
 * If we're booting paravirtualized under a hypervisor, then there are
 * more options: we may already be running PAE, and the pagetable may
 * or may not be based in swapper_pg_dir.  In any case,
 * paravirt_pagetable_init() will set up swapper_pg_dir
 * appropriately for the rest of the initialization to work.
 *
 * In general, pagetable_init() assumes that the pagetable may already
 * be partially populated, and so it avoids stomping on any existing
 * mappings.
 */
void __init early_ioremap_page_table_range_init(void)
{
	pgd_t *pgd_base = swapper_pg_dir;
	unsigned long vaddr, end;

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, pgd_base);
}

#define DEFAULT_PTE_MASK ~(_PAGE_NX | _PAGE_GLOBAL)
/* Bits supported by the hardware: */
pteval_t __supported_pte_mask __read_mostly = DEFAULT_PTE_MASK;
/* Bits allowed in normal kernel mappings: */
pteval_t __default_kernel_pte_mask __read_mostly = DEFAULT_PTE_MASK;

/*
 * All of RAM fits into lowmem - but if user wants highmem
 * artificially via the highmem=x boot parameter then create
 * it:
 */
static void __init lowmem_pfn_init(void)
{
	/* max_low_pfn is 0, we already have early_res support */
	max_low_pfn = max_pfn;
}

/*
 * We have more RAM than fits into lowmem - we try to put it into
 * highmem, also taking the highmem=x boot parameter into account:
 */
static void __init highmem_pfn_init(void)
{
	max_low_pfn = MAXMEM_PFN;

	/* Maximum memory usable is what is directly addressable */
	max_pfn = MAXMEM_PFN;
}

/*
 * Determine low and high memory ranges:
 */
void __init find_low_pfn_range(void)
{
	/* it could update max_pfn */

	if (max_pfn <= MAXMEM_PFN)
		lowmem_pfn_init();
	else
		highmem_pfn_init();
}

void __init initmem_init(void)
{
	high_memory = (void *)__va(max_low_pfn * PAGE_SIZE - 1) + 1;

	max_mapnr = max_low_pfn;
}

static void __init paging_init(void)
{
	__flush_tlb_all();

	/*
	 * NOTE: at this point the bootmem allocator is fully available.
	 */
	zone_sizes_init();
}

/*
 * Test if the WP bit works in supervisor mode. It isn't supported on 386's
 * and also on some strange 486's. All 586+'s are OK. This used to involve
 * black magic jumps to work around some nasty CPU bugs, but fortunately the
 * switch to using exceptions got rid of all that.
 */
static void __init test_wp_bit(void)
{
	char z = 0;

	__set_fixmap(FIX_WP_TEST, __pa_symbol(empty_zero_page), PAGE_KERNEL_RO);

	if (copy_to_kernel_nofault((char *)fix_to_virt(FIX_WP_TEST), &z, 1)) {
		clear_fixmap(FIX_WP_TEST);
		return;
	}

	panic("Linux doesn't support CPUs with broken WP.");
}

void __init mem_init(void)
{
	BUG_ON(!mem_map);

	/* this will put all low memory onto the freelists */
	memblock_free_all();

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#define __FIXADDR_TOP (-PAGE_SIZE)
#define high_memory (-128UL << 20)
	BUILD_BUG_ON(VMALLOC_START >= VMALLOC_END);
#undef high_memory
#undef __FIXADDR_TOP

	BUG_ON(VMALLOC_START >= VMALLOC_END);
	BUG_ON((unsigned long)high_memory > VMALLOC_START);

	test_wp_bit();
}
