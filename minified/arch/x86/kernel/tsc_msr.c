// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - TSC frequency via MSR not needed
 */

#include <linux/kernel.h>
#include <asm/tsc.h>

unsigned long cpu_khz_from_msr(void)
{
	return 0;
}
