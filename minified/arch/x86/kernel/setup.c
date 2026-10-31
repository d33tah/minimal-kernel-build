#include <linux/console.h>

#include <linux/memblock.h>

#include <asm/e820/api.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/setup.h>

unsigned long max_pfn_mapped;

unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

struct boot_params boot_params;

struct cpuinfo_x86 boot_cpu_data __read_mostly;

__visible unsigned long mmu_cr4_features __ro_after_init;

struct screen_info screen_info;

void *__init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);

	_brk_end = (_brk_end + mask) & ~mask;
	BUG_ON((char *)(_brk_end + size) > __brk_limit);

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

static void __init reserve_brk(void)
{
	if (_brk_end > _brk_start)
		memblock_reserve(__pa_symbol(_brk_start),
				 _brk_end - _brk_start);

	_brk_start = 0;
}

static void __init early_reserve_memory(void)
{
	memblock_reserve(__pa_symbol(_text),
			 (unsigned long)__end_of_kernel_reserve -
				 (unsigned long)_text);

	/*
	 * The first megabyte (real-mode IVT, BIOS data area, EBDA, VGA and
	 * BIOS ROM windows) is never handed to the allocator.
	 */
	memblock_reserve(0, SZ_1M);
}

void __init setup_arch(void)
{
	clone_pgd_range(swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);

	__flush_tlb_all();

	idt_setup_early_traps();
	early_cpu_init();
	jump_label_init();

	screen_info = boot_params.screen_info;
	/* edid_info/apm_info/ist_info/saved_video_mode copies removed - never read */

	early_reserve_memory();

	e820__memory_setup();

	setup_initial_init_mm(_text, _etext, _edata, (void *)_brk_end);

	tsc_early_init();

	max_pfn = e820__end_of_ram_pfn();

	/* max_possible_pfn assignment removed - never read */

	find_low_pfn_range();

	early_alloc_pgt_buf();

	reserve_brk();

	memblock_set_current_limit(ISA_END_ADDRESS);
	e820__memblock_setup();

	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
	       (max_pfn_mapped << PAGE_SHIFT) - 1);

	init_mem_mapping();

	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

	memblock_set_current_limit(get_max_mapped());

	initmem_init();

	/* dma_contiguous_reserve, hugetlb_cma_reserve, reserve_crashkernel - stubs */

	native_pagetable_init();

	sync_initial_page_table();

	conswitchp = &vga_con;

	register_refined_jiffies(CLOCK_TICK_RATE);
}
