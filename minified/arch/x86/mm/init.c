#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/sched/mm.h>
#include <asm/e820/api.h>
#include <asm/setup.h>
#include <asm/tlb.h>
extern int after_bootmem;

void *alloc_low_pages(unsigned int num);
void early_ioremap_page_table_range_init(void);
unsigned long kernel_physical_mapping_init(unsigned long start,
					   unsigned long end,
					   unsigned long page_size_mask,
					   pgprot_t prot);
void zone_sizes_init(void);

static unsigned long __initdata pgt_buf_start;
static unsigned long __initdata pgt_buf_end;
static unsigned long __initdata pgt_buf_top;

static unsigned long min_pfn_mapped;

static bool __initdata can_use_brk_pgt = true;

__ref void *alloc_low_pages(unsigned int num)
{
	unsigned long pfn;
	int i;

	if (after_bootmem) {
		unsigned int order;

		order = get_order((unsigned long)num << PAGE_SHIFT);
		return (void *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO, order);
	}

	if ((pgt_buf_end + num) > pgt_buf_top || !can_use_brk_pgt) {
		unsigned long ret = 0;

		if (min_pfn_mapped < max_pfn_mapped) {
			ret = memblock_phys_alloc_range(
				PAGE_SIZE * num, PAGE_SIZE,
				min_pfn_mapped << PAGE_SHIFT,
				max_pfn_mapped << PAGE_SHIFT);
		}
		if (!ret && can_use_brk_pgt)
			ret = __pa(extend_brk(PAGE_SIZE * num, PAGE_SIZE));

		if (!ret)
			panic("alloc_low_pages: can not alloc memory");

		pfn = ret >> PAGE_SHIFT;
	} else {
		pfn = pgt_buf_end;
		pgt_buf_end += num;
	}

	for (i = 0; i < num; i++) {
		void *adr;

		adr = __va((pfn + i) << PAGE_SHIFT);
		clear_page(adr);
	}

	return __va(pfn << PAGE_SHIFT);
}

#define INIT_PGD_PAGE_TABLES 1 /* Reduced from 3 for minimal boot */

#define INIT_PGD_PAGE_COUNT (2 * INIT_PGD_PAGE_TABLES)

#define INIT_PGT_BUF_SIZE (INIT_PGD_PAGE_COUNT * PAGE_SIZE)
RESERVE_BRK(early_pgt_alloc, INIT_PGT_BUF_SIZE);
void __init early_alloc_pgt_buf(void)
{
	unsigned long tables = INIT_PGT_BUF_SIZE;
	phys_addr_t base;

	base = __pa(extend_brk(tables, PAGE_SIZE));

	pgt_buf_start = base >> PAGE_SHIFT;
	pgt_buf_end = pgt_buf_start;
	pgt_buf_top = pgt_buf_start + (tables >> PAGE_SHIFT);
}

int after_bootmem;

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

static int page_size_mask;

static inline void cr4_set_bits_and_update_boot(unsigned long mask)
{
	mmu_cr4_features |= mask;
	cr4_set_bits(mask);
}

#define NR_RANGE_MR 3

static int __meminit save_mr(struct map_range *mr, int nr_range,
			     unsigned long start_pfn, unsigned long end_pfn,
			     unsigned long page_size_mask)
{
	if (start_pfn < end_pfn) {
		if (nr_range >= NR_RANGE_MR)
			panic("run out of range for init_memory_mapping\n");
		mr[nr_range].start = start_pfn << PAGE_SHIFT;
		mr[nr_range].end = end_pfn << PAGE_SHIFT;
		mr[nr_range].page_size_mask = page_size_mask;
		nr_range++;
	}

	return nr_range;
}

static int __meminit split_mem_range(struct map_range *mr, int nr_range,
				     unsigned long start, unsigned long end)
{
	unsigned long start_pfn, end_pfn, limit_pfn;
	unsigned long pfn;

	limit_pfn = PFN_DOWN(end);

	pfn = start_pfn = PFN_DOWN(start);

	if (pfn == 0)
		end_pfn = PFN_DOWN(PMD_SIZE);
	else
		end_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
	if (end_pfn > limit_pfn)
		end_pfn = limit_pfn;
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
		pfn = end_pfn;
	}

	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));

	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				   page_size_mask & (1 << PG_LEVEL_2M));
		pfn = end_pfn;
	}

	start_pfn = pfn;
	end_pfn = limit_pfn;
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);

	return nr_range;
}

struct range pfn_mapped[E820_MAX_ENTRIES];
int nr_pfn_mapped;

unsigned long __ref init_memory_mapping(unsigned long start, unsigned long end,
					pgprot_t prot)
{
	struct map_range mr[NR_RANGE_MR];
	unsigned long ret = 0;
	int nr_range, i;

	memset(mr, 0, sizeof(mr));
	nr_range = split_mem_range(mr, 0, start, end);

	for (i = 0; i < nr_range; i++)
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask, prot);

	{
		unsigned long start_pfn = start >> PAGE_SHIFT;
		unsigned long end_pfn = ret >> PAGE_SHIFT;
		nr_pfn_mapped =
			add_range_with_merge(pfn_mapped, E820_MAX_ENTRIES,
					     nr_pfn_mapped, start_pfn, end_pfn);
		nr_pfn_mapped = clean_sort_range(pfn_mapped, E820_MAX_ENTRIES);
		max_pfn_mapped = max(max_pfn_mapped, end_pfn);
		if (start_pfn < (1UL << (32 - PAGE_SHIFT)))
			max_low_pfn_mapped =
				max(max_low_pfn_mapped,
				    min(end_pfn, 1UL << (32 - PAGE_SHIFT)));
	}

	return ret >> PAGE_SHIFT;
}

static unsigned long __init init_range_memory_mapping(unsigned long r_start,
						      unsigned long r_end)
{
	unsigned long start_pfn, end_pfn;
	unsigned long mapped_ram_size = 0;
	int i;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		u64 start = clamp_val(PFN_PHYS(start_pfn), r_start, r_end);
		u64 end = clamp_val(PFN_PHYS(end_pfn), r_start, r_end);
		if (start >= end)
			continue;

		can_use_brk_pgt = max(start, (u64)pgt_buf_end << PAGE_SHIFT) >=
				  min(end, (u64)pgt_buf_top << PAGE_SHIFT);
		init_memory_mapping(start, end, PAGE_KERNEL);
		mapped_ram_size += end - start;
		can_use_brk_pgt = true;
	}

	return mapped_ram_size;
}

static void __init memory_map_top_down(unsigned long map_start,
				       unsigned long map_end)
{
	max_pfn_mapped = 0;
	min_pfn_mapped = map_end >> PAGE_SHIFT;
	init_range_memory_mapping(map_start, map_end);
}

void __init init_mem_mapping(void)
{
	unsigned long end;

	if (boot_cpu_has(X86_FEATURE_PSE)) {
		page_size_mask |= 1 << PG_LEVEL_2M;
		cr4_set_bits_and_update_boot(X86_CR4_PSE);
	}
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (boot_cpu_has(X86_FEATURE_PGE)) {
		cr4_set_bits_and_update_boot(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}
	__default_kernel_pte_mask = __supported_pte_mask;
	/* No GB pages on x86-32 */
	/* setup_pcid empty - PCID not supported on 32-bit */

	end = max_low_pfn << PAGE_SHIFT;

	init_memory_mapping(0, ISA_END_ADDRESS, PAGE_KERNEL);

	memory_map_top_down(ISA_END_ADDRESS, end);

	early_ioremap_page_table_range_init();

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
}

void __ref free_initmem(void)
{
	/* Skip freeing init memory - hangs with 4MB RAM */
}

void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	free_area_init(max_zone_pfns);
}

__visible DEFINE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate) = {
	.loaded_mm = &init_mm,
	.next_asid = 1,
	.cr4 = ~0UL,
};

void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	mm->mmap_base = __TASK_UNMAPPED_BASE(DEFAULT_MAP_WINDOW);
}
