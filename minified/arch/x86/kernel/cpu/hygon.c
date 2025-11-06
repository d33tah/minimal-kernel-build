// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal stub - Hygon CPU support not needed
 */
#include <asm/cpu.h>
#include "cpu.h"

static void early_init_hygon(struct cpuinfo_x86 *c)
{
}

static void bsp_init_hygon(struct cpuinfo_x86 *c)
{
}

static void init_hygon(struct cpuinfo_x86 *c)
{
	early_init_hygon(c);
	bsp_init_hygon(c);
}

static const struct cpu_dev hygon_cpu_dev = {
	.c_vendor	= "Hygon",
	.c_ident	= { "HygonGenuine" },
	.c_early_init   = early_init_hygon,
	.c_bsp_init	= bsp_init_hygon,
	.c_init		= init_hygon,
	.c_x86_vendor	= X86_VENDOR_HYGON,
};

cpu_dev_register(hygon_cpu_dev);
