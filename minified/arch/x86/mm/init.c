#include <linux/memblock.h>

#include <asm/cpufeature.h>
#include <asm/e820/api.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>

#include "mm_internal.h"

static unsigned long __initdata pgt_buf_start;
static unsigned long __initdata pgt_buf_end;
static unsigned long __initdata pgt_buf_top;

static unsigned long min_pfn_mapped;

static bool __initdata can_use_brk_pgt = true;

/*
 * Pages returned are already directly mapped.
 *
 * Changing that is likely to break Xen, see commit:
 *
 *    279b706 x86,xen: introduce x86_init.mapping.pagetable_reserve
 *
 * for detailed information.
 */
__ref void *alloc_low_pages(unsigned int num)
{
	unsigned long pfn;
	int i;

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

#define INIT_PGD_PAGE_TABLES 3

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

struct map_range {
	unsigned long start, end;
	unsigned page_size_mask;
};

static int page_size_mask;

/*
 * Save some of cr4 feature set we're using (e.g.  Pentium 4MB
 * enable and PPro Global page enable), so that any CPU's that boot
 * up after us can get the correct flags. Invoked on the boot CPU.
 */
static inline void cr4_set_bits_and_update_boot(unsigned long mask)
{
	mmu_cr4_features |= mask;
	cr4_set_bits(mask);
}

static void __init probe_page_size_mask(void)
{
	if (boot_cpu_has(X86_FEATURE_PSE))
		page_size_mask |= 1 << PG_LEVEL_2M;

	/* Enable PSE if available */
	if (boot_cpu_has(X86_FEATURE_PSE))
		cr4_set_bits_and_update_boot(X86_CR4_PSE);

	/* Enable PGE if available */
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (boot_cpu_has(X86_FEATURE_PGE)) {
		cr4_set_bits_and_update_boot(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}

	/* By the default is everything supported: */
	__default_kernel_pte_mask = __supported_pte_mask;
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

/*
 * adjust the page_size_mask for small range to go with
 *	big page size instead small one if nearby are ram too.
 */
static void __ref adjust_range_page_size_mask(struct map_range *mr,
					      int nr_range)
{
	int i;

	for (i = 0; i < nr_range; i++) {
		if ((page_size_mask & (1 << PG_LEVEL_2M)) &&
		    !(mr[i].page_size_mask & (1 << PG_LEVEL_2M))) {
			unsigned long start = round_down(mr[i].start, PMD_SIZE);
			unsigned long end = round_up(mr[i].end, PMD_SIZE);

			if ((end >> PAGE_SHIFT) > max_low_pfn)
				continue;

			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1 << PG_LEVEL_2M;
		}
	}
}

static int __meminit split_mem_range(struct map_range *mr, int nr_range,
				     unsigned long start, unsigned long end)
{
	unsigned long start_pfn, end_pfn, limit_pfn;
	unsigned long pfn;
	int i;

	limit_pfn = PFN_DOWN(end);

	/* head if not big page alignment ? */
	pfn = start_pfn = PFN_DOWN(start);

	/*
	 * Don't use a large page for the first 2/4MB of memory
	 * because there are often fixed size MTRRs in there
	 * and overlapping MTRRs into large pages can cause
	 * slowdowns.
	 */
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

	/* tail is not big page (1G) alignment */
	start_pfn = round_up(pfn, PFN_DOWN(PMD_SIZE));
	end_pfn = round_down(limit_pfn, PFN_DOWN(PMD_SIZE));

	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				   page_size_mask & (1 << PG_LEVEL_2M));
		pfn = end_pfn;
	}

	/* tail is not big page (2M) alignment */
	start_pfn = pfn;
	end_pfn = limit_pfn;
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);

	adjust_range_page_size_mask(mr, nr_range);

	/* try to merge same page size and continuous */
	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i + 1].start ||
		    mr[i].page_size_mask != mr[i + 1].page_size_mask)
			continue;

		/* move it */
		old_start = mr[i].start;
		memmove(&mr[i], &mr[i + 1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start;
		nr_range--;
	}

	return nr_range;
}

static unsigned long __ref init_memory_mapping(unsigned long start,
					       unsigned long end, pgprot_t prot)
{
	struct map_range mr[NR_RANGE_MR];
	unsigned long ret = 0;
	int nr_range, i;

	memset(mr, 0, sizeof(mr));
	nr_range = split_mem_range(mr, 0, start, end);

	for (i = 0; i < nr_range; i++)
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask, prot);

	max_pfn_mapped = max(max_pfn_mapped, ret >> PAGE_SHIFT);

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

		/*
		 * if it is overlapping with brk pgt, we need to
		 * alloc pgt buf from memblock instead.
		 */
		can_use_brk_pgt = max(start, (u64)pgt_buf_end << PAGE_SHIFT) >=
				  min(end, (u64)pgt_buf_top << PAGE_SHIFT);
		init_memory_mapping(start, end, PAGE_KERNEL);
		mapped_ram_size += end - start;
		can_use_brk_pgt = true;
	}

	return mapped_ram_size;
}

static unsigned long __init get_new_step_size(unsigned long step_size)
{
	/*
	 * Initial mapped size is PMD_SIZE (2M).
	 * We can not set step_size to be PUD_SIZE (1G) yet.
	 * In worse case, when we cross the 1G boundary, and
	 * PG_LEVEL_2M is not set, we will need 1+1+512 pages (2M + 8k)
	 * to map 1G range with PTE. Hence we use one less than the
	 * difference of page table level shifts.
	 *
	 * Don't need to worry about overflow in the top-down case, on 32bit,
	 * when step_size is 0, round_down() returns 0 for start, and that
	 * turns it into 0x100000000ULL.
	 * In the bottom-up case, round_up(x, 0) returns 0 though too, which
	 * needs to be taken into consideration by the code below.
	 */
	return step_size << (PMD_SHIFT - PAGE_SHIFT - 1);
}

/**
 * memory_map_top_down - Map [map_start, map_end) top down
 * @map_start: start address of the target memory range
 * @map_end: end address of the target memory range
 *
 * This function will setup direct mapping for memory range
 * [map_start, map_end) in top-down. That said, the page tables
 * will be allocated at the end of the memory, and we map the
 * memory in top-down.
 */
static void __init memory_map_top_down(unsigned long map_start,
				       unsigned long map_end)
{
	unsigned long real_end, last_start;
	unsigned long step_size, addr;
	unsigned long mapped_ram_size = 0;

	/*
	 * MKB_FOOTPRINT: probe for the page tables the LOWMEM_PAGES cap can
	 * need, page-aligned (upstream: a free PMD_SIZE block aligned at
	 * PMD_SIZE, 4 MiB here, so -m 8M and less failed the probe and
	 * memblock_phys_free(0, PMD_SIZE) un-reserved the kernel image).
	 */
	addr = memblock_phys_alloc_range(PAGE_TABLE_SIZE(LOWMEM_PAGES)
						 << PAGE_SHIFT,
					 PAGE_SIZE, map_start, map_end);
	memblock_phys_free(addr, PAGE_TABLE_SIZE(LOWMEM_PAGES) << PAGE_SHIFT);
	real_end = addr + (PAGE_TABLE_SIZE(LOWMEM_PAGES) << PAGE_SHIFT);

	/* step_size need to be small so pgt_buf from BRK could cover it */
	step_size = PMD_SIZE;
	max_pfn_mapped = 0; /* will get exact value next */
	min_pfn_mapped = real_end >> PAGE_SHIFT;
	last_start = real_end;

	/*
	 * We start from the top (end of memory) and go to the bottom.
	 * The memblock_find_in_range() gets us a block of RAM from the
	 * end of RAM in [min_pfn_mapped, max_pfn_mapped) used as new pages
	 * for page table.
	 */
	while (last_start > map_start) {
		unsigned long start;

		if (last_start > step_size) {
			start = round_down(last_start - 1, step_size);
			if (start < map_start)
				start = map_start;
		} else
			start = map_start;
		mapped_ram_size += init_range_memory_mapping(start, last_start);
		last_start = start;
		min_pfn_mapped = last_start >> PAGE_SHIFT;
		if (mapped_ram_size >= step_size)
			step_size = get_new_step_size(step_size);
	}

	if (real_end < map_end)
		init_range_memory_mapping(real_end, map_end);
}

void __init init_mem_mapping(void)
{
	unsigned long end;

	probe_page_size_mask();

	end = max_low_pfn << PAGE_SHIFT;

	/* the ISA range is always mapped regardless of memory holes */
	init_memory_mapping(0, ISA_END_ADDRESS, PAGE_KERNEL);

	memory_map_top_down(ISA_END_ADDRESS, end);

	early_ioremap_page_table_range_init();

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
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
	.cr4 = ~0UL, /* fail hard if we screw up cr4 shadow initialization */
};
