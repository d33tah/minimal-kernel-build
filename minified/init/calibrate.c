 
 

#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/percpu.h>

unsigned long lpj_fine;
unsigned long preset_lpj;

static int __init lpj_setup(char *str)
{
	preset_lpj = simple_strtoul(str, NULL, 0);
	return 1;
}

__setup("lpj=", lpj_setup);

static DEFINE_PER_CPU(unsigned long, cpu_loops_per_jiffy) = { 0 };

unsigned long __attribute__((weak)) calibrate_delay_is_known(void)
{
	return 0;
}

void __attribute__((weak)) calibration_delay_done(void)
{
}

void calibrate_delay(void)
{
	 
	unsigned long lpj = 12500000;
	int this_cpu = smp_processor_id();

	if (per_cpu(cpu_loops_per_jiffy, this_cpu)) {
		lpj = per_cpu(cpu_loops_per_jiffy, this_cpu);
	} else if (preset_lpj) {
		lpj = preset_lpj;
	}

	per_cpu(cpu_loops_per_jiffy, this_cpu) = lpj;
	loops_per_jiffy = lpj;
	calibration_delay_done();
}
