#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/pci.h>

#include <asm/acpi.h>
#include <asm/bios_ebda.h>
#include <asm/paravirt.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820/api.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/io_apic.h>
#define hpet_readl(a) 0
#define default_setup_hpet_msi	NULL
#include <asm/memtype.h>
#include <asm/tsc.h>
#include <asm/mach_traps.h>

void x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }
bool __init bool_x86_init_noop(void) { return false; }

struct x86_init_ops x86_init __initdata = {

	.resources = {
		.probe_roms		= probe_roms,
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= e820__memory_setup_default,
	},

	.mpparse = {
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		.intr_mode_select	= apic_intr_mode_select,
		.intr_mode_init		= apic_intr_mode_init,
	},

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

	.timers = {
		.setup_percpu_clockev	= setup_boot_APIC_clock,
		.timer_init		= hpet_time_init,
		.wallclock_init		= x86_init_noop,
	},

	.hyper = {
		.init_platform		= x86_init_noop,
		.guest_late_init	= x86_init_noop,
		.x2apic_available	= bool_x86_init_noop,
		.msi_ext_dest_id	= bool_x86_init_noop,
		.init_mem_mapping	= x86_init_noop,
		.init_after_bootmem	= x86_init_noop,
	},

	.acpi = {
		.set_root_pointer	= x86_default_set_root_pointer,
		.get_root_pointer	= x86_default_get_root_pointer,
	},
};

static void enc_status_change_prepare_noop(unsigned long vaddr, int npages, bool enc) { }
static bool enc_status_change_finish_noop(unsigned long vaddr, int npages, bool enc) { return false; }
static bool enc_tlb_flush_required_noop(bool enc) { return false; }
static bool enc_cache_flush_required_noop(void) { return false; }

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu			= native_calibrate_cpu_early,
	.calibrate_tsc			= native_calibrate_tsc,
	.get_wallclock			= mach_get_cmos_time,
	.set_wallclock			= mach_set_rtc_mmss,
	.get_nmi_reason			= default_get_nmi_reason,

	.guest = {
		.enc_status_change_prepare = enc_status_change_prepare_noop,
		.enc_status_change_finish  = enc_status_change_finish_noop,
		.enc_tlb_flush_required	   = enc_tlb_flush_required_noop,
		.enc_cache_flush_required  = enc_cache_flush_required_noop,
	},
};
