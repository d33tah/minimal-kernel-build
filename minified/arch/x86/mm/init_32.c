
#include <linux/memblock.h>

#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/pgtable_areas.h>

void *alloc_low_pages(unsigned int num);
static inline void *alloc_low_page(void)
{
	return alloc_low_pages(1);
}
void early_ioremap_page_table_range_init(void);
unsigned long kernel_physical_mapping_init(unsigned long start,
					   unsigned long end,
					   unsigned long page_size_mask,
					   pgprot_t prot);
void zone_sizes_init(void);
extern int after_bootmem;

static pmd_t *__init one_md_table_init(pgd_t *pgd)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd_table;

	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);
	pmd_table = pmd_offset(pud, 0);

	return pmd_table;
}

static pte_t *__init one_page_table_init(pmd_t *pmd)
{
	if (!(pmd_val(*pmd) & _PAGE_PRESENT)) {
		pte_t *page_table = (pte_t *)alloc_low_page();

		paravirt_alloc_pte(&init_mm, __pa(page_table) >> PAGE_SHIFT);
		set_pmd(pmd, __pmd(__pa(page_table) | _PAGE_TABLE));
	}

	return pte_offset_kernel(pmd, 0);
}

pmd_t *__init populate_extra_pmd(unsigned long vaddr)
{
	int pgd_idx = pgd_index(vaddr);
	int pmd_idx = pmd_index(vaddr);

	return one_md_table_init(swapper_pg_dir + pgd_idx) + pmd_idx;
}

pte_t *__init populate_extra_pte(unsigned long vaddr)
{
	int pte_idx = pte_index(vaddr);
	pmd_t *pmd;

	pmd = populate_extra_pmd(vaddr);
	return one_page_table_init(pmd) + pte_idx;
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
	pte_t *pte = NULL;
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
			one_page_table_init(pmd);

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

void __init sync_initial_page_table(void)
{
	clone_pgd_range(initial_page_table + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY, KERNEL_PGD_PTRS);

	clone_pgd_range(initial_page_table,
			swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			min(KERNEL_PGD_PTRS, KERNEL_PGD_BOUNDARY));
}

void __init native_pagetable_init(void)
{
	unsigned long pfn, va;
	pgd_t *pgd, *base = swapper_pg_dir;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (pfn = max_low_pfn; pfn < 1 << (32 - PAGE_SHIFT); pfn++) {
		va = PAGE_OFFSET + (pfn << PAGE_SHIFT);
		pgd = base + pgd_index(va);

		p4d = p4d_offset(pgd, va);
		pud = pud_offset(p4d, va);
		pmd = pmd_offset(pud, va);
		if (!pmd_present(*pmd))
			break;

		/* pmd_large check kept - it checks actual page flags */

		pte = pte_offset_kernel(pmd, va);
		if (!pte_present(*pte))
			break;

		pte_clear(NULL, va, pte);
	}
	paravirt_alloc_pmd(&init_mm, __pa(base) >> PAGE_SHIFT);
	paging_init();
}

void __init early_ioremap_page_table_range_init(void)
{
	pgd_t *pgd_base = swapper_pg_dir;
	unsigned long vaddr, end;

	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, pgd_base);
	early_ioremap_reset();
}

#define DEFAULT_PTE_MASK ~(_PAGE_NX | _PAGE_GLOBAL)
pteval_t __supported_pte_mask __read_mostly = DEFAULT_PTE_MASK;
pteval_t __default_kernel_pte_mask __read_mostly = DEFAULT_PTE_MASK;

void __init find_low_pfn_range(void)
{
	/* No highmem: max_pfn always <= MAXMEM_PFN for this minimal kernel */
	max_low_pfn = max_pfn;
}

void __init initmem_init(void)
{
	high_memory = (void *)__va(max_low_pfn * PAGE_SIZE - 1) + 1;

	max_mapnr = max_low_pfn; /* !HIGHMEM */
}

void __init paging_init(void)
{
	__flush_tlb_all();
	sparse_init();
	zone_sizes_init();
}

void __init mem_init(void)
{
	memblock_free_all();
	after_bootmem = 1;
}
