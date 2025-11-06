// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - performance frequency scaling not needed
 */
#include <linux/cpufreq.h>

void arch_scale_freq_tick(void)
{
}

unsigned int arch_freq_get_on_cpu(int cpu)
{
	return 0;
}
