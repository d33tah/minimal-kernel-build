#include <linux/console.h>
#include <asm/sections.h>

extern int initrd_below_start_ok;
extern unsigned long initrd_start, initrd_end;
extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;
#include <linux/memblock.h>
#include <linux/kdev_t.h>

extern dev_t ROOT_DEV;

#include <asm/apic.h>
#include <asm/io.h>
void reserve_bios_regions(void);
#include <asm/cpu.h>
#include <asm/e820/api.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

struct boot_params boot_params;

static struct resource rodata_resource = { .name = "Kernel rodata",
					   .start = 0,
					   .end = 0,
					   .flags = IORESOURCE_BUSY |
						    IORESOURCE_SYSTEM_RAM };

static struct resource data_resource = { .name = "Kernel data",
					 .start = 0,
					 .end = 0,
					 .flags = IORESOURCE_BUSY |
						  IORESOURCE_SYSTEM_RAM };

static struct resource code_resource = { .name = "Kernel code",
					 .start = 0,
					 .end = 0,
					 .flags = IORESOURCE_BUSY |
						  IORESOURCE_SYSTEM_RAM };

static struct resource bss_resource = { .name = "Kernel bss",
					.start = 0,
					.end = 0,
					.flags = IORESOURCE_BUSY |
						 IORESOURCE_SYSTEM_RAM };

struct cpuinfo_x86 new_cpu_data;

struct cpuinfo_x86 boot_cpu_data __read_mostly;

__visible unsigned long mmu_cr4_features __ro_after_init;

struct screen_info screen_info;

static char __initdata command_line[COMMAND_LINE_SIZE];

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
	early_ioremap_init();

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;

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

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;
	e820__memory_setup();
	code_resource.start = __pa_symbol(_text);
	code_resource.end = __pa_symbol(_etext) - 1;
	rodata_resource.start = __pa_symbol(__start_rodata);
	rodata_resource.end = __pa_symbol(__end_rodata) - 1;
	data_resource.start = __pa_symbol(_sdata);
	data_resource.end = __pa_symbol(_edata) - 1;
	bss_resource.start = __pa_symbol(__bss_start);
	bss_resource.end = __pa_symbol(__bss_stop) - 1;

	strscpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	if (boot_cpu_has(X86_FEATURE_NX))
		__supported_pte_mask |= _PAGE_NX;
	else
		__supported_pte_mask &= ~_PAGE_NX;

	parse_early_param();

	tsc_early_init();

	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &rodata_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	{
		u64 start = __pa_symbol(_text);
		u64 size = __pa_symbol(_end) - start;
		if (!e820__mapped_all(start, start + size, E820_TYPE_RAM)) {
			pr_warn(".text .data .bss are not marked as E820_TYPE_RAM!\n");
			e820__range_remove(start, size, E820_TYPE_RAM, 0);
			e820__range_add(start, size, E820_TYPE_RAM);
		}
	}
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

	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
	       (max_pfn_mapped << PAGE_SHIFT) - 1);

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

	generic_apic_probe();

	x86_init.resources.reserve_resources();
	conswitchp = &vga_con;
}

void __init i386_reserve_resources(void)
{
}
