/* sadbg debug function removed */
#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/dma-map-ops.h>
/* efi.h inlined - only efi_enabled stub used */
#include <linux/screen_info.h>
static inline bool efi_enabled(int feature)
{
	return false;
}

#include <linux/initrd.h>
#include <linux/memblock.h>
/* panic_notifier_list extern removed - never registered into */
#include <linux/pci.h>
#include <linux/root_dev.h>
#include <linux/hugetlb.h>
#define tboot_probe() \
	do {          \
	} while (0)
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

#include <uapi/linux/mount.h>

#include <asm/apic.h>
#include <asm/numa.h>
#include <asm/bios_ebda.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/e820/api.h>
#include <asm/tlbflush.h>
#include <asm/io_apic.h>
/* kasan.h, mce.h removed - KASAN macros never used */
#include <asm/memtype.h>
/* mtrr.h removed - header is empty */
#include <asm/realmode.h>
/* pci-direct.h inlined - extern declarations removed as never called */
#include <linux/types.h>
/* prom.h inlined - only asm/setup.h needed for COMMAND_LINE_SIZE */
#include <asm/setup.h>
#include <asm/proto.h>
#include <asm/unwind.h>
/* asm/vsyscall.h removed - empty */
#include <linux/vmalloc.h>

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

/* def_to_bigsmp, apm_info, ist_info, edid_info removed - set but never read */

__visible unsigned long mmu_cr4_features __ro_after_init;

struct screen_info screen_info;

extern int root_mountflags;

/* saved_video_mode, RAMDISK_* macros removed - never used */

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

/* reserve_brk inlined into setup_arch */

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

/* relocate_initrd, early_reserve_initrd, reserve_initrd inlined into setup_arch */

/* parse_setup_data, memblock_x86_reserve_range_setup_data inlined into setup_arch/early_reserve_memory */

static struct resource standard_io_resources[] = {
	{ .name = "dma1",
	  .start = 0x00,
	  .end = 0x1f,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1",
	  .start = 0x20,
	  .end = 0x21,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0",
	  .start = 0x40,
	  .end = 0x43,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1",
	  .start = 0x50,
	  .end = 0x53,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard",
	  .start = 0x60,
	  .end = 0x60,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard",
	  .start = 0x64,
	  .end = 0x64,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg",
	  .start = 0x80,
	  .end = 0x8f,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2",
	  .start = 0xa0,
	  .end = 0xa1,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2",
	  .start = 0xc0,
	  .end = 0xdf,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu",
	  .start = 0xf0,
	  .end = 0xff,
	  .flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

void __init reserve_standard_io_resources(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);
}

/* trim_snb_memory removed - empty stub */

/* trim_bios_range inlined into setup_arch */
/* e820_add_kernel_range, early_reserve_memory inlined into setup_arch */

/* dump_kernel_offset and kernel_offset_notifier removed - stub that did nothing */

void x86_configure_nx(void)
{
	if (boot_cpu_has(X86_FEATURE_NX))
		__supported_pte_mask |= _PAGE_NX;
	else
		__supported_pte_mask &= ~_PAGE_NX;
}

/* x86_report_nx removed - empty stub */

void __init setup_arch(char **cmdline_p)
{
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));

	clone_pgd_range(swapper_pg_dir + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);

	__flush_tlb_all();

	/* olpc_ofw_detect removed - empty stub */
	idt_setup_early_traps();
	early_cpu_init();
	jump_label_init();
	early_ioremap_init();
	/* setup_olpc_ofw_pgd removed - empty stub */

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	/* edid_info, apm_info, ist_info, saved_video_mode, bootloader_type/version removed - never read */

	/* x86_init.oem.arch_setup removed - is x86_init_noop */

	/* Inlined early_reserve_memory */
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
				break;
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
					break;
				}
				indirect = (struct setup_indirect *)data->data;
				if (indirect->type != SETUP_INDIRECT)
					memblock_reserve(indirect->addr,
							 indirect->len);
			}
			pa_data = pa_next;
			early_memunmap(data, len);
		}
	}
	reserve_bios_regions();

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;
	e820__memory_setup();
	/* parse_setup_data inlined */
	{
		struct setup_data *data;
		u64 pa_data, pa_next;

		pa_data = boot_params.hdr.setup_data;
		while (pa_data) {
			data = early_memremap(pa_data, sizeof(*data));
			pa_next = data->next;
			if (data->type == SETUP_E820_EXT)
				e820__memory_setup_extended(
					pa_data,
					data->len + sizeof(struct setup_data));
			early_memunmap(data, sizeof(*data));
			pa_data = pa_next;
		}
	}
	/* copy_edd removed - empty stub */

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	/* setup_initial_init_mm call removed - function is empty stub */

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

	x86_configure_nx();

	parse_early_param();
	/* efi_enabled(EFI_BOOT) always false - efi_memblock_x86_reserve_range call removed */
	/* x86_report_nx removed - was empty stub */

	e820__reserve_setup_data();
	/* e820__finish_early_params removed - function was empty */

	/* dmi_setup, init_hypervisor_platform removed - empty stubs */

	tsc_early_init();
	/* x86_init.resources.probe_roms removed - was empty stub */

	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &rodata_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	/* Inlined e820_add_kernel_range */
	{
		u64 start = __pa_symbol(_text);
		u64 size = __pa_symbol(_end) - start;
		if (!e820__mapped_all(start, start + size, E820_TYPE_RAM)) {
			pr_warn(".text .data .bss are not marked as E820_TYPE_RAM!\n");
			e820__range_remove(start, size, E820_TYPE_RAM, 0);
			e820__range_add(start, size, E820_TYPE_RAM);
		}
	}
	/* Inlined trim_bios_range */
	e820__range_update(0, PAGE_SIZE, E820_TYPE_RAM, E820_TYPE_RESERVED);
	e820__range_remove(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_TYPE_RAM, 1);
	e820__update_table(e820_table);

	max_pfn = e820__end_of_ram_pfn();

	/* MTRR disabled - pat_disable, init_cache_modes removed (empty stubs) */

	find_low_pfn_range();

	/* find_smp_config removed - default_find_smp_config is x86_init_noop */

	early_alloc_pgt_buf();

	/* Inlined reserve_brk */
	if (_brk_end > _brk_start)
		memblock_reserve(__pa_symbol(_brk_start),
				 _brk_end - _brk_start);
	_brk_start = 0;
	/* cleanup_highmap removed - empty stub */

	memblock_set_current_limit(ISA_END_ADDRESS);
	e820__memblock_setup();
	/* sev_setup_arch, e820__memblock_alloc_reserved_mpc_new - empty stubs */

	printk(KERN_DEBUG "initial memory mapped: [mem 0x00000000-%#010lx]\n",
	       (max_pfn_mapped << PAGE_SHIFT) - 1);

	reserve_real_mode();

	init_mem_mapping();

	/* idt_setup_early_pf removed - empty stub */

	mmu_cr4_features = __read_cr4() & ~X86_CR4_PCIDE;

	memblock_set_current_limit(get_max_mapped());

	/* setup_log_buf removed - empty stub */
	/* efi_enabled always false - secure boot switch removed */
	/* reserve_initrd inlined */
	{
		u64 ramdisk_image = get_ramdisk_image();
		u64 ramdisk_size = get_ramdisk_size();
		u64 ramdisk_end = PAGE_ALIGN(ramdisk_image + ramdisk_size);

		if (boot_params.hdr.type_of_loader && ramdisk_image &&
		    ramdisk_size) {
			initrd_start = 0;
			printk(KERN_INFO "RAMDISK: [mem %#010llx-%#010llx]\n",
			       ramdisk_image, ramdisk_end - 1);

			if (pfn_range_is_mapped(PFN_DOWN(ramdisk_image),
						PFN_DOWN(ramdisk_end))) {
				initrd_start = ramdisk_image + PAGE_OFFSET;
				initrd_end = initrd_start + ramdisk_size;
			} else {
				u64 area_size = PAGE_ALIGN(ramdisk_size);
				relocated_ramdisk = memblock_phys_alloc_range(
					area_size, PAGE_SIZE, 0,
					PFN_PHYS(max_pfn_mapped));
				if (!relocated_ramdisk)
					panic("Cannot find place for new RAMDISK of size %lld\n",
					      ramdisk_size);
				initrd_start = relocated_ramdisk + PAGE_OFFSET;
				initrd_end = initrd_start + ramdisk_size;
				printk(KERN_INFO
				       "Allocated new RAMDISK: [mem %#010llx-%#010llx]\n",
				       relocated_ramdisk,
				       relocated_ramdisk + ramdisk_size - 1);
				copy_from_early_mem((void *)initrd_start,
						    ramdisk_image,
						    ramdisk_size);
				printk(KERN_INFO
				       "Move RAMDISK from [mem %#010llx-%#010llx] to [mem %#010llx-%#010llx]\n",
				       ramdisk_image,
				       ramdisk_image + ramdisk_size - 1,
				       relocated_ramdisk,
				       relocated_ramdisk + ramdisk_size - 1);
				memblock_phys_free(ramdisk_image,
						   ramdisk_end - ramdisk_image);
			}
		}
	}
	/* acpi_table_upgrade is empty stub */
	/* io_delay_init removed - was empty stub */

	/* early_platform_quirks removed - was empty stub */

	initmem_init();

	/* dma_contiguous_reserve, hugetlb_cma_reserve, reserve_crashkernel - stubs */

	x86_init.paging.pagetable_init();

	/* kasan_init removed - empty stub */
	sync_initial_page_table();

	tboot_probe();

	generic_apic_probe();

	/* x86_dtb_init, get_smp_config, init_apic_mappings - empty stubs */

	/* init_cpu_to_node and init_gi_nodes - empty stubs */

	/* io_apic_init_mappings removed - empty stub */

	/* x86_init.hyper.guest_late_init removed - is x86_init_noop */

	e820__reserve_resources();
	/* e820__register_nosave_regions call removed - stub function */

	x86_init.resources.reserve_resources();

	e820__setup_pci_gap();
	/* efi_enabled always false - condition simplified */
	conswitchp = &vga_con;
	/* x86_init.timers.wallclock_init, register_refined_jiffies removed */
}

static struct resource video_ram_resource = { .name = "Video RAM area",
					      .start = 0xa0000,
					      .end = 0xbffff,
					      .flags = IORESOURCE_BUSY |
						       IORESOURCE_MEM };

void __init i386_reserve_resources(void)
{
	request_resource(&iomem_resource, &video_ram_resource);
	reserve_standard_io_resources();
}

/* register_kernel_offset_dumper initcall removed - callback was empty stub */
