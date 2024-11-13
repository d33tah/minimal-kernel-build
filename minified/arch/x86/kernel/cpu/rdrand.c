// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 */

#include <asm/processor.h>
#include <asm/archrandom.h>
#include <asm/sections.h>

static int __init x86_rdrand_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_RDRAND);
	setup_clear_cpu_cap(X86_FEATURE_RDSEED);
	return 1;
}
__setup("nordrand", x86_rdrand_setup);

/*
 * RDRAND has Built-In-Self-Test (BIST) that runs on every invocation.
 * Run the instruction a few times as a sanity check.
 * If it fails, it is simple to disable RDRAND here.
 */
#define SANITY_CHECK_LOOPS 8

