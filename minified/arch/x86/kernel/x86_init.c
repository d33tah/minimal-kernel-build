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
/* hpet_enable duplicate removed - already defined in time.c */
#define hpet_readl(a) 0
#define default_setup_hpet_msi NULL
#include <asm/memtype.h>
#include <asm/tsc.h>
#include <asm/mach_traps.h>
#define native_create_pci_msi_domain NULL

void x86_init_noop(void)
{
}
/* x86_init_uint_noop, bool_x86_init_noop, x86_op_int_noop, etc removed - never used */
/* x86_wallclock_init removed - never called */

struct x86_init_ops x86_init __initdata = {

	.resources = {
		/* .probe_roms removed - never called */
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= e820__memory_setup_default,
	},

	/* .mpparse removed - never called */

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		/* intr_mode_select, intr_mode_init, create_pci_msi_domain removed - never called */
	},

	/* .oem removed - never accessed (arch_setup, banner commented out in setup.c) */

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

	.timers = {
		.setup_percpu_clockev	= setup_boot_APIC_clock,
		.timer_init		= hpet_time_init,
		/* .wallclock_init removed - never called */
	},

	/* .iommu, .pci, .hyper, .acpi removed - never accessed */
};

/* x86_cpuinit removed - never accessed */
/* default_nmi_init, enc_*_noop functions removed - never called */

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu = native_calibrate_cpu_early,
	.calibrate_tsc = native_calibrate_tsc,
	.get_wallclock = mach_get_cmos_time,
	/* .set_wallclock, .iommu_shutdown, .save/restore_sched_clock_state removed - never called */
	.is_untracked_pat_range = is_ISA_range,
	/* .nmi_init removed - never called */
	.get_nmi_reason = default_get_nmi_reason,
	/* .hyper, .guest removed - never called */
};

/* x86_apic_ops removed - never accessed */
