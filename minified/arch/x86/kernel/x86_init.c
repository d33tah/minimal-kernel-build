#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/bios_ebda.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820/api.h>
#include <asm/time.h>
#include <asm/irq.h>
#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H
#include <asm/processor.h>
#include <asm/cpufeature.h>
typedef unsigned long long cycles_t;
static inline cycles_t get_cycles(void)
{
	return rdtsc();
}
#define get_cycles get_cycles
extern void tsc_early_init(void);
extern unsigned long native_calibrate_cpu_early(void);
extern unsigned long native_calibrate_tsc(void);
#endif /* _ASM_X86_TSC_H */
#include <asm/io.h>
#define NMI_REASON_PORT 0x61
static inline unsigned char default_get_nmi_reason(void)
{
	return inb(NMI_REASON_PORT);
}

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
	.get_wallclock = mach_get_cmos_time,
	.is_untracked_pat_range = is_ISA_range,
	.get_nmi_reason = default_get_nmi_reason,
};
