#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/memblock.h>
/* linux/sched/task.h, set_memory.h removed - not used */
#include <asm/e820/api.h>
#include <asm/page.h>
#include <asm/page_types.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/proto.h>
#include <asm/cpufeature.h>
/* pti_check_boottime_disable removed - empty stub */
#include <asm/text-patching.h>

#include "mm_internal.h"

static uint16_t __cachemode2pte_tbl[_PAGE_CACHE_MODE_NUM] = {
	[_PAGE_CACHE_MODE_WB] = 0 | 0,
	[_PAGE_CACHE_MODE_WC] = 0 | _PAGE_PCD,
	[_PAGE_CACHE_MODE_UC_MINUS] = 0 | _PAGE_PCD,
	[_PAGE_CACHE_MODE_UC] = _PAGE_PWT | _PAGE_PCD,
	[_PAGE_CACHE_MODE_WT] = 0 | _PAGE_PCD,
	[_PAGE_CACHE_MODE_WP] = 0 | _PAGE_PCD,
};

unsigned long cachemode2protval(enum page_cache_mode pcm)
{
	if (likely(pcm == 0))
		return 0;
	return __cachemode2pte_tbl[pcm];
}

/* __pte2cachemode_tbl removed - was never accessed (~10 LOC) */
/* x86_has_pat_wp, pgprot2cachemode removed - never called (~15 LOC) */

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

early_param_on_off("gbpages", "nogbpages", direct_gbpages,
		   CONFIG_X86_DIRECT_GBPAGES);

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

static int page_size_mask;

static inline void cr4_set_bits_and_update_boot(unsigned long mask)
{
	mmu_cr4_features |= mask;
	if (trampoline_cr4_features)
		*trampoline_cr4_features = mmu_cr4_features;
	cr4_set_bits(mask);
}

/* probe_page_size_mask inlined into init_mem_mapping, setup_pcid is empty */

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
		if ((page_size_mask & (1 << PG_LEVEL_1G)) &&
		    !(mr[i].page_size_mask & (1 << PG_LEVEL_1G))) {
			unsigned long start = round_down(mr[i].start, PUD_SIZE);
			unsigned long end = round_up(mr[i].end, PUD_SIZE);

			if (memblock_is_region_memory(start, end - start))
				mr[i].page_size_mask |= 1 << PG_LEVEL_1G;
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

	if (!after_bootmem)
		adjust_range_page_size_mask(mr, nr_range);

	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i + 1].start ||
		    mr[i].page_size_mask != mr[i + 1].page_size_mask)
			continue;

		old_start = mr[i].start;
		memmove(&mr[i], &mr[i + 1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start;
		nr_range--;
	}

	return nr_range;
}

struct range pfn_mapped[E820_MAX_ENTRIES];
int nr_pfn_mapped;

/* add_pfn_range_mapped inlined into init_memory_mapping */

bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn)
{
	int i;

	for (i = 0; i < nr_pfn_mapped; i++)
		if ((start_pfn >= pfn_mapped[i].start) &&
		    (end_pfn <= pfn_mapped[i].end))
			return true;

	return false;
}

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

	/* add_pfn_range_mapped inlined */
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

static unsigned long __init get_new_step_size(unsigned long step_size)
{
	return step_size << (PMD_SHIFT - PAGE_SHIFT - 1);
}

static void __init memory_map_top_down(unsigned long map_start,
				       unsigned long map_end)
{
	unsigned long real_end, last_start;
	unsigned long step_size;
	unsigned long addr;
	unsigned long mapped_ram_size = 0;

	addr = memblock_phys_alloc_range(PMD_SIZE, PMD_SIZE, map_start,
					 map_end);
	memblock_phys_free(addr, PMD_SIZE);
	real_end = addr + PMD_SIZE;

	step_size = PMD_SIZE;
	max_pfn_mapped = 0;
	min_pfn_mapped = real_end >> PAGE_SHIFT;
	last_start = real_end;

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

static void __init memory_map_bottom_up(unsigned long map_start,
					unsigned long map_end)
{
	unsigned long next, start;
	unsigned long mapped_ram_size = 0;

	unsigned long step_size = PMD_SIZE;

	start = map_start;
	min_pfn_mapped = start >> PAGE_SHIFT;

	while (start < map_end) {
		if (step_size && map_end - start > step_size) {
			next = round_up(start + 1, step_size);
			if (next > map_end)
				next = map_end;
		} else {
			next = map_end;
		}

		mapped_ram_size += init_range_memory_mapping(start, next);
		start = next;

		if (mapped_ram_size >= step_size)
			step_size = get_new_step_size(step_size);
	}
}

/* init_trampoline removed - empty stub */

void __init init_mem_mapping(void)
{
	unsigned long end;

	/* probe_page_size_mask inlined */
	if (boot_cpu_has(X86_FEATURE_PSE)) {
		page_size_mask |= 1 << PG_LEVEL_2M;
		cr4_set_bits_and_update_boot(X86_CR4_PSE);
	} else {
		direct_gbpages = 0;
	}
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (boot_cpu_has(X86_FEATURE_PGE)) {
		cr4_set_bits_and_update_boot(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}
	__default_kernel_pte_mask = __supported_pte_mask;
	if (direct_gbpages && boot_cpu_has(X86_FEATURE_GBPAGES)) {
		printk(KERN_INFO "Using GB pages for direct mapping\n");
		page_size_mask |= 1 << PG_LEVEL_1G;
	} else {
		direct_gbpages = 0;
	}
	/* setup_pcid empty - PCID not supported on 32-bit */

	end = max_low_pfn << PAGE_SHIFT;

	init_memory_mapping(0, ISA_END_ADDRESS, PAGE_KERNEL);

	if (memblock_bottom_up()) {
		unsigned long kernel_end = __pa_symbol(_end);

		memory_map_bottom_up(kernel_end, end);
		memory_map_bottom_up(ISA_END_ADDRESS, kernel_end);
	} else {
		memory_map_top_down(ISA_END_ADDRESS, end);
	}

	early_ioremap_page_table_range_init();

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();

	/* x86_init.hyper.init_mem_mapping removed - is x86_init_noop */
	/* early_memtest removed - empty stub */
}

void __init poking_init(void)
{
	spinlock_t *ptl;
	pte_t *ptep;

	poking_mm = copy_init_mm();
	BUG_ON(!poking_mm);

	/* RANDOMIZE_BASE not enabled */
	poking_addr = TASK_UNMAPPED_BASE;

	if (((poking_addr + PAGE_SIZE) & ~PMD_MASK) == 0)
		poking_addr += PAGE_SIZE;

	ptep = get_locked_pte(poking_mm, poking_addr, &ptl);
	BUG_ON(!ptep);
	pte_unmap_unlock(ptep, ptl);
}

/* free_init_pages, free_kernel_image_pages removed - zero callers */

void __ref free_initmem(void)
{
	/* Skip freeing init memory - hangs with 4MB RAM */
}

/* free_initrd_mem removed - never called (~4 LOC) */

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
