/* Delay loop calibration - minimal stub */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/percpu.h>

/* preset_lpj removed - never assigned, always zero */

static DEFINE_PER_CPU(unsigned long, cpu_loops_per_jiffy) = { 0 };

void calibrate_delay(void)
{
	unsigned long lpj = 12500000;
	int this_cpu = smp_processor_id();

	if (per_cpu(cpu_loops_per_jiffy, this_cpu))
		lpj = per_cpu(cpu_loops_per_jiffy, this_cpu);
	/* preset_lpj check removed - it was never assigned */

	per_cpu(cpu_loops_per_jiffy, this_cpu) = lpj;
	loops_per_jiffy = lpj;
}
