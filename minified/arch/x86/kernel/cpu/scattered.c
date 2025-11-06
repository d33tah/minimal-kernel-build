// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - scattered CPU features not needed
 */
#include <linux/cpu.h>
#include <asm/processor.h>

#include "cpu.h"

void init_scattered_cpuid_features(struct cpuinfo_x86 *c)
{
}
