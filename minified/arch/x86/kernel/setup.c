// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 * This file contains the setup_arch() code, which handles the architecture-dependent
 * parts of early kernel initialization.
 */
#include <linux/console.h>

#include <linux/memblock.h>

#include <asm/e820/api.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/tsc.h>

unsigned long max_pfn_mapped;

static unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

struct boot_params boot_params;

struct cpuinfo_x86 boot_cpu_data __read_mostly;

__visible unsigned long mmu_cr4_features __ro_after_init;

/*
 * Setup options
 */
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

	/* Mark brk area as locked down and no longer taking any
	   new allocations */
	_brk_start = 0;
}

static void __init early_reserve_memory(void)
{
	/*
	 * Reserve the memory occupied by the kernel between _text and
	 * __end_of_kernel_reserve symbols. Any kernel sections after the
	 * __end_of_kernel_reserve symbol must be explicitly reserved with a
	 * separate memblock_reserve() or they will be discarded.
	 */
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
	/*
	 * copy kernel address range established so far and switch
	 * to the proper swapper page table
	 */
	clone_pgd_range(swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);

	/*
	 * Note: Quark X1000 CPUs advertise PGE incorrectly and require
	 * a cr3 based tlb flush, so the following __flush_tlb_all()
	 * will not flush anything because the CPU quirk which clears
	 * X86_FEATURE_PGE has not been invoked yet. Though due to the
	 * load_cr3() above the TLB has been flushed already. The
	 * quirk is invoked before subsequent calls to __flush_tlb_all()
	 * so proper operation is guaranteed.
	 */
	__flush_tlb_all();

	idt_setup_early_traps();
	early_cpu_init();

	screen_info = boot_params.screen_info;

	/*
	 * Do some memory reservations *before* memory is added to memblock, so
	 * memblock allocations won't overwrite it.
	 *
	 * After this point, everything still needed from the boot loader or
	 * firmware or kernel text should be early reserved or marked not RAM in
	 * e820. All other memory is free game.
	 *
	 * This call needs to happen before e820__memory_setup() which calls the
	 * xen_memory_setup() on Xen dom0 which relies on the fact that those
	 * early reservations have happened already.
	 */
	early_reserve_memory();

	e820__memory_setup();

	setup_initial_init_mm(_text, _etext, _edata, (void *)_brk_end);

	tsc_early_init();

	max_pfn = e820__end_of_ram_pfn();

	/* max_low_pfn get updated here */
	find_low_pfn_range();

	early_alloc_pgt_buf();

	/*
	 * Need to conclude brk, before e820__memblock_setup()
	 * it could use memblock_find_in_range, could overlap with
	 * brk area.
	 */
	reserve_brk();

	memblock_set_current_limit(ISA_END_ADDRESS);
	e820__memblock_setup();

	init_mem_mapping();

	/*
	 * Update mmu_cr4_features (and, indirectly, trampoline_cr4_features)
	 * with the current CR4 value.  This may not be necessary, but
	 * auditing all the early-boot CR4 manipulation would be needed to
	 * rule it out.
	 *
	 * Mask off features that don't work outside long mode (just
	 * PCIDE for now).
	 */
	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

	memblock_set_current_limit(get_max_mapped());

	initmem_init();

	native_pagetable_init();

	conswitchp = &vga_con;
}
