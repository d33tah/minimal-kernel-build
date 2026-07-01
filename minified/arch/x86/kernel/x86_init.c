#include <linux/init.h>

#include <asm/acpi.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/e820/api.h>
#include <asm/time.h>
#include <asm/irq.h>
#define hpet_readl(a) 0
#include <asm/tsc.h>
#include <asm/mach_traps.h>

void x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }

struct x86_init_ops x86_init __initdata = {

	.resources = {
		.reserve_resources	= x86_init_noop,
		.memory_setup		= e820__memory_setup_default,
	},

	.mpparse = {
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
	},

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

	.timers = {
		.timer_init		= hpet_time_init,
	},
};

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu			= native_calibrate_cpu_early,
	.calibrate_tsc			= native_calibrate_tsc,
	.get_nmi_reason			= default_get_nmi_reason,
};
