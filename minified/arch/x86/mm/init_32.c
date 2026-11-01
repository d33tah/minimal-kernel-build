#include <linux/memblock.h>

#include <linux/swap.h>
#include <asm/cpufeature.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>

#include <asm/pgtable_areas.h>

#include "mm_internal.h"

static pmd_t *__init one_md_table_init(pgd_t *pgd)
{
	return pmd_offset(pud_offset(p4d_offset(pgd, 0), 0), 0);
}

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

			if (use_pse) {
				unsigned int addr2;
				pgprot_t prot = PAGE_KERNEL_LARGE;

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
		__flush_tlb_all();

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

	for (pfn = max_low_pfn; pfn < 1 << (32 - PAGE_SHIFT); pfn++) {
		va = PAGE_OFFSET + (pfn << PAGE_SHIFT);
		pgd = base + pgd_index(va);

		pmd = pmd_offset(pud_offset(p4d_offset(pgd, va), va), va);
		if (!pmd_present(*pmd))
			break;

		if (pmd_large(*pmd)) {
			BUG_ON(1);
		}

		pte = pte_offset_kernel(pmd, va);
		if (!pte_present(*pte))
			break;

		pte_clear(NULL, va, pte);
	}

	__flush_tlb_all();

	zone_sizes_init();
}

/* Page tables for the fixmap (FIX_WP_TEST) range. */
void __init fixmap_page_table_init(void)
{
	pgd_t *pgd_base = swapper_pg_dir;
	unsigned long vaddr, end;

	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, pgd_base);
}

#define DEFAULT_PTE_MASK ~(_PAGE_NX | _PAGE_GLOBAL)
pteval_t __supported_pte_mask __read_mostly = DEFAULT_PTE_MASK;
pteval_t __default_kernel_pte_mask __read_mostly = DEFAULT_PTE_MASK;

static void __init lowmem_pfn_init(void)
{
	/* max_low_pfn is 0, we already have early_res support */
	max_low_pfn = max_pfn;
}

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

	memblock_free_all();

#define __FIXADDR_TOP (-PAGE_SIZE)
#define high_memory (-128UL << 20)
	BUILD_BUG_ON(VMALLOC_START >= VMALLOC_END);
#undef high_memory
#undef __FIXADDR_TOP

	BUG_ON(VMALLOC_START >= VMALLOC_END);
	BUG_ON((unsigned long)high_memory > VMALLOC_START);

	test_wp_bit();
}
