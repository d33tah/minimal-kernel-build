#include <linux/cache.h>
#include <asm/setup.h>
#include <asm/e820/api.h>
#include <asm/pgtable_types.h>
#include <asm/msr.h>
#include <asm/irq.h>
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H
typedef unsigned long long cycles_t;
static inline cycles_t get_cycles(void)
{
	return rdtsc();
}
#define get_cycles get_cycles
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
#endif /* _ASM_X86_TSC_H */

struct x86_init_ops x86_init __initdata = {

	.resources = {
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= e820__memory_setup_default,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
	},

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

};

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu = native_calibrate_cpu_early,
	.calibrate_tsc = native_calibrate_tsc,
};
