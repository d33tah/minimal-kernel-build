#include <asm/sections.h>
#include <linux/timex.h>

extern unsigned long initrd_start, initrd_end;
extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;
#include <linux/memblock.h>
/* kdev_t inlined */
#define MINORBITS 20
#define MKDEV(ma, mi) (((ma) << MINORBITS) | (mi))
static __always_inline dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

extern dev_t ROOT_DEV;

#include <asm/io.h>

#define BIOS_RAM_SIZE_KB_PTR 0x413
#define BIOS_START_MIN 0x20000U
#define BIOS_START_MAX 0x9f000U
static void __init reserve_bios_regions(void)
{
	unsigned int bios_start, ebda_start;
	if (!x86_platform.legacy.reserve_bios_regions)
		return;
	bios_start = *(unsigned short *)__va(BIOS_RAM_SIZE_KB_PTR);
	bios_start <<= 10;
	if (bios_start < BIOS_START_MIN || bios_start > BIOS_START_MAX)
		bios_start = BIOS_START_MAX;
	ebda_start = *(unsigned short *)phys_to_virt(0x40E);
	ebda_start <<= 4;
	if (ebda_start >= BIOS_START_MIN && ebda_start < bios_start)
		bios_start = ebda_start;
	memblock_reserve(bios_start, 0x100000 - bios_start);
}

#include <asm/cpu.h>
#include <asm/e820/api.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

struct boot_params boot_params;

struct cpuinfo_x86 new_cpu_data;

struct cpuinfo_x86 boot_cpu_data __read_mostly;

__visible unsigned long mmu_cr4_features __ro_after_init;

static char __initdata command_line[COMMAND_LINE_SIZE];

void *__init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	_brk_end = (_brk_end + mask) & ~mask;

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

static u64 __init get_ramdisk_image(void)
{
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;

	ramdisk_image |= (u64)boot_params.ext_ramdisk_image << 32;

	if (ramdisk_image == 0)
		ramdisk_image = phys_initrd_start;

	return ramdisk_image;
}
static u64 __init get_ramdisk_size(void)
{
	u64 ramdisk_size = boot_params.hdr.ramdisk_size;

	ramdisk_size |= (u64)boot_params.ext_ramdisk_size << 32;

	if (ramdisk_size == 0)
		ramdisk_size = phys_initrd_size;

	return ramdisk_size;
}

void __init reserve_standard_io_resources(void)
{
}

void __init setup_arch(char **cmdline_p)
{
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));

	clone_pgd_range(swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);

	__flush_tlb_all();

	idt_setup_early_traps();
	early_cpu_init();
	jump_label_init();
	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);

	memblock_reserve(__pa_symbol(_text),
			 (unsigned long)__end_of_kernel_reserve -
				 (unsigned long)_text);
	memblock_reserve(0, SZ_64K);
	{
		u64 ramdisk_image = get_ramdisk_image();
		u64 ramdisk_size = get_ramdisk_size();
		u64 ramdisk_end = PAGE_ALIGN(ramdisk_image + ramdisk_size);
		if (boot_params.hdr.type_of_loader && ramdisk_image &&
		    ramdisk_size)
			memblock_reserve(ramdisk_image,
					 ramdisk_end - ramdisk_image);
	}
	reserve_bios_regions();

	e820__memory_setup();

	strscpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();

	tsc_early_init();

	e820__range_update(0, PAGE_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);
	e820__range_remove(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_TYPE_RAM, 1);
	max_pfn = e820__end_of_ram_pfn();

	find_low_pfn_range();

	early_alloc_pgt_buf();

	if (_brk_end > _brk_start)
		memblock_reserve(__pa_symbol(_brk_start),
				 _brk_end - _brk_start);
	_brk_start = 0;

	memblock_set_current_limit(ISA_END_ADDRESS);
	e820__memblock_setup();

	memblock_reserve(0, SZ_1M);

	init_mem_mapping();

	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

	memblock_set_current_limit(get_max_mapped());

	{
		u64 ramdisk_image = get_ramdisk_image();
		u64 ramdisk_size = get_ramdisk_size();

		if (boot_params.hdr.type_of_loader && ramdisk_image &&
		    ramdisk_size) {
			initrd_start = ramdisk_image + PAGE_OFFSET;
			initrd_end = initrd_start + ramdisk_size;
		}
	}

	initmem_init();

	/* dma_contiguous_reserve, hugetlb_cma_reserve, reserve_crashkernel - stubs */

	x86_init.paging.pagetable_init();

	sync_initial_page_table();

	x86_init.resources.reserve_resources();
}

void __init i386_reserve_resources(void)
{
}
