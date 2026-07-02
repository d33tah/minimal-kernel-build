#include <linux/console.h>

#include <linux/initrd.h>
#include <linux/memblock.h>
extern struct atomic_notifier_head panic_notifier_list;

#include <asm/apic.h>
#include <asm/e820/api.h>
#include <asm/bios_ebda.h>
#include <asm/memtype.h>
#include <asm/realmode.h>
#include <asm/prom.h>
#include <asm/efi.h>

unsigned long max_pfn_mapped;



unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end   = (unsigned long)__brk_base;

struct boot_params boot_params;


struct cpuinfo_x86 new_cpu_data;

struct cpuinfo_x86 boot_cpu_data __read_mostly;

__visible unsigned long mmu_cr4_features __ro_after_init;


struct screen_info screen_info;


static char __initdata command_line[COMMAND_LINE_SIZE];

void * __init extend_brk(size_t size, size_t align)
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

u64 relocated_ramdisk;


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

static void __init early_reserve_initrd(void)
{
	 
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		 

	memblock_reserve(ramdisk_image, ramdisk_end - ramdisk_image);
}

static void __init reserve_initrd(void)
{
	 
	u64 ramdisk_image = get_ramdisk_image();
	u64 ramdisk_size  = get_ramdisk_size();
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		 

	initrd_start = 0;

	printk(KERN_INFO "RAMDISK: [mem %#010llx-%#010llx]\n", ramdisk_image,
			ramdisk_end - 1);

	if (pfn_range_is_mapped(PFN_DOWN(ramdisk_image),
				PFN_DOWN(ramdisk_end))) {
		 
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		return;
	}

	{
		u64 area_size = PAGE_ALIGN(ramdisk_size);

		relocated_ramdisk = memblock_phys_alloc_range(area_size, PAGE_SIZE, 0,
							      PFN_PHYS(max_pfn_mapped));
		if (!relocated_ramdisk)
			panic("Cannot find place for new RAMDISK of size %lld\n",
			      ramdisk_size);

		initrd_start = relocated_ramdisk + PAGE_OFFSET;
		initrd_end   = initrd_start + ramdisk_size;
		printk(KERN_INFO "Allocated new RAMDISK: [mem %#010llx-%#010llx]\n",
		       relocated_ramdisk, relocated_ramdisk + ramdisk_size - 1);

		copy_from_early_mem((void *)initrd_start, ramdisk_image, ramdisk_size);

		printk(KERN_INFO "Move RAMDISK from [mem %#010llx-%#010llx] to"
			" [mem %#010llx-%#010llx]\n",
			ramdisk_image, ramdisk_image + ramdisk_size - 1,
			relocated_ramdisk, relocated_ramdisk + ramdisk_size - 1);
	}

	memblock_phys_free(ramdisk_image, ramdisk_end - ramdisk_image);
}


static void __init parse_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data, pa_next;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		u32 data_len, data_type;

		data = early_memremap(pa_data, sizeof(*data));
		data_len = data->len + sizeof(struct setup_data);
		data_type = data->type;
		pa_next = data->next;
		early_memunmap(data, sizeof(*data));

		/*
		 * CONFIG_OF and CONFIG_EFI are both absent on this build, so the
		 * SETUP_DTB (add_dtb) and SETUP_EFI (parse_efi_setup) handlers were
		 * empty stubs -- only SETUP_E820_EXT does real work here.
		 */
		if (data_type == SETUP_E820_EXT)
			e820__memory_setup_extended(pa_data, data_len);
		pa_data = pa_next;
	}
}

static void __init memblock_x86_reserve_range_setup_data(void)
{
	struct setup_indirect *indirect;
	struct setup_data *data;
	u64 pa_data, pa_next;
	u32 len;

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		if (!data) {
			pr_warn("setup: failed to memremap setup_data entry\n");
			return;
		}

		len = sizeof(*data);
		pa_next = data->next;

		memblock_reserve(pa_data, sizeof(*data) + data->len);

		if (data->type == SETUP_INDIRECT) {
			len += data->len;
			early_memunmap(data, sizeof(*data));
			data = early_memremap(pa_data, len);
			if (!data) {
				pr_warn("setup: failed to memremap indirect setup_data\n");
				return;
			}

			indirect = (struct setup_indirect *)data->data;

			if (indirect->type != SETUP_INDIRECT)
				memblock_reserve(indirect->addr, indirect->len);
		}

		pa_data = pa_next;
		early_memunmap(data, len);
	}
}


static void __init trim_snb_memory(void)
{
	/* Stub: Sandy Bridge graphics workaround not needed for minimal kernel */
}

static void __init trim_bios_range(void)
{
	 
	e820__range_update(0, PAGE_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);

	 
	e820__range_remove(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_TYPE_RAM, 1);

	e820__update_table(e820_table);
}

static void __init e820_add_kernel_range(void)
{
	u64 start = __pa_symbol(_text);
	u64 size = __pa_symbol(_end) - start;

	 
	if (e820__mapped_all(start, start + size, E820_TYPE_RAM))
		return;

	pr_warn(".text .data .bss are not marked as E820_TYPE_RAM!\n");
	e820__range_remove(start, size, E820_TYPE_RAM, 0);
	e820__range_add(start, size, E820_TYPE_RAM);
}

static void __init early_reserve_memory(void)
{
	 
	memblock_reserve(__pa_symbol(_text),
			 (unsigned long)__end_of_kernel_reserve - (unsigned long)_text);

	 
	memblock_reserve(0, SZ_64K);

	early_reserve_initrd();

	memblock_x86_reserve_range_setup_data();

	reserve_bios_regions();
	trim_snb_memory();
}

static void __init x86_report_nx(void)
{
	/* Stub: NX reporting not needed for minimal kernel */
}


void __init setup_arch(char **cmdline_p)
{
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));

	 
	clone_pgd_range(swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);
	 
	__flush_tlb_all();

	idt_setup_early_traps();
	early_cpu_init();
	jump_label_init();
	early_ioremap_init();

	screen_info = boot_params.screen_info;
	/* edid_info/apm_info/ist_info/saved_video_mode copies removed - never read */

	/* x86_init.oem.arch_setup() removed - dispatched to x86_init_noop */


	early_reserve_memory();

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;
	e820__memory_setup();
	parse_setup_data();

	setup_initial_init_mm(_text, _etext, _edata, (void *)_brk_end);


	strscpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;


	if (boot_cpu_has(X86_FEATURE_NX))
		__supported_pte_mask |= _PAGE_NX;
	else
		__supported_pte_mask &= ~_PAGE_NX;

	parse_early_param();

	x86_report_nx();

	e820__reserve_setup_data();
	e820__finish_early_params();


	tsc_early_init();


	e820_add_kernel_range();
	trim_bios_range();


	max_pfn = e820__end_of_ram_pfn();


	pat_disable("PAT support disabled because CONFIG_MTRR is disabled in the kernel.");

	/* max_possible_pfn assignment removed - never read */


	init_cache_modes();


	find_low_pfn_range();

	 
	find_smp_config();

	early_alloc_pgt_buf();

	 
	reserve_brk();

	memblock_set_current_limit(ISA_END_ADDRESS);
	e820__memblock_setup();

	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
			(max_pfn_mapped<<PAGE_SHIFT) - 1);

	 
	reserve_real_mode();

	init_mem_mapping();


	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

	memblock_set_current_limit(get_max_mapped());

	 

	 
	setup_log_buf(1);

	reserve_initrd();

	initmem_init();

	/* dma_contiguous_reserve, hugetlb_cma_reserve, reserve_crashkernel - stubs */

	x86_init.paging.pagetable_init();


	sync_initial_page_table();

	/* e820__reserve_resources() removed: built write-only iomem-tree state */

	x86_init.resources.reserve_resources();

	conswitchp = &vga_con;
	/* x86_init.oem.banner() removed - dispatched to x86_init_noop */
	/* x86_init.timers.wallclock_init() removed - dispatched to x86_init_noop */

	register_refined_jiffies(CLOCK_TICK_RATE);
}


