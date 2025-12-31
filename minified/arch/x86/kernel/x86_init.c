#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/pci.h>

#include <asm/acpi.h>
#include <asm/bios_ebda.h>
#include <asm/paravirt.h>
#include <asm/pci_x86.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820/api.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/io_apic.h>
static inline int hpet_enable(void)
{
	return 0;
}
#define hpet_readl(a) 0
#define default_setup_hpet_msi NULL
#include <asm/memtype.h>
#include <asm/tsc.h>
#include <asm/mach_traps.h>
#define native_create_pci_msi_domain NULL

void x86_init_noop(void)
{
}
void __init x86_init_uint_noop(unsigned int unused)
{
}
/* iommu_init_noop removed - x86_init.iommu never used */
/* iommu_shutdown_noop removed - never called */
bool __init bool_x86_init_noop(void)
{
	return false;
}
void x86_op_int_noop(int cpu)
{
}
/* set_rtc_noop, get_rtc_noop removed - defined but never used */
static __init void x86_wallclock_init(void)
{
}

struct x86_init_ops x86_init __initdata = {

	.resources = {
		.probe_roms		= x86_init_noop,
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= e820__memory_setup_default,
	},

	.mpparse = {
		.setup_ioapic_ids	= x86_init_noop,
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		.intr_mode_select	= x86_init_noop,
		.intr_mode_init		= x86_init_noop,
		.create_pci_msi_domain	= native_create_pci_msi_domain,
	},

	.oem = {
		.arch_setup		= x86_init_noop,
		.banner			= default_banner,
	},

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

	.timers = {
		.setup_percpu_clockev	= setup_boot_APIC_clock,
		.timer_init		= hpet_time_init,
		.wallclock_init		= x86_wallclock_init,
	},

	/* .iommu removed - never used */

	.pci = {
		.init			= x86_default_pci_init,
		.init_irq		= x86_default_pci_init_irq,
		.fixup_irqs		= x86_default_pci_fixup_irqs,
	},

	.hyper = {
		.init_platform		= x86_init_noop,
		.guest_late_init	= x86_init_noop,
		.x2apic_available	= bool_x86_init_noop,
		.msi_ext_dest_id	= bool_x86_init_noop,
		.init_mem_mapping	= x86_init_noop,
		.init_after_bootmem	= x86_init_noop,
	},
	/* .acpi removed - never used */
};

struct x86_cpuinit_ops x86_cpuinit = {
	.early_percpu_clock_init = x86_init_noop,
	.setup_percpu_clockev = setup_secondary_APIC_clock,
};

static void default_nmi_init(void)
{
}
/* enc_*_noop functions removed - x86_platform.guest never called */

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu = native_calibrate_cpu_early,
	.calibrate_tsc = native_calibrate_tsc,
	.get_wallclock = mach_get_cmos_time,
	/* .set_wallclock, .iommu_shutdown, .save/restore_sched_clock_state removed - never called */
	.is_untracked_pat_range = is_ISA_range,
	.nmi_init = default_nmi_init,
	.get_nmi_reason = default_get_nmi_reason,
	.hyper.pin_vcpu = x86_op_int_noop,
	/* .guest removed - never called */
};

struct x86_apic_ops x86_apic_ops __ro_after_init = {
	.io_apic_read = native_io_apic_read,
	.restore = native_restore_boot_irq_mode,
};
