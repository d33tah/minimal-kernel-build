// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - CPU cache information via sysfs not needed for minimal kernel
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/cacheinfo.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/sysfs.h>

/* Stubbed */
static DEFINE_PER_CPU(struct cpu_cacheinfo, ci_cpu_cacheinfo);

struct cpu_cacheinfo *get_cpu_cacheinfo(unsigned int cpu)
{
	return &per_cpu(ci_cpu_cacheinfo, cpu);
}

int __weak cache_setup_acpi(unsigned int cpu)
{
	return -ENOTSUPP;
}

unsigned int coherency_max_size;

int __weak init_cache_level(unsigned int cpu)
{
	return -ENOENT;
}

int __weak populate_cache_leaves(unsigned int cpu)
{
	return -ENOENT;
}
