
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/node.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/tick.h>

#include <linux/mod_devicetable.h>
#include <asm/cpufeature.h>
#ifndef CPU_FEATURE_TYPEFMT
#define CPU_FEATURE_TYPEFMT	"%s"
#endif
#ifndef CPU_FEATURE_TYPEVAL
#define CPU_FEATURE_TYPEVAL	ELF_PLATFORM
#endif
#define module_cpu_feature_match(x, __initfunc)			\
static struct cpu_feature const __maybe_unused cpu_feature_match_ ## x[] = \
	{ { .feature = cpu_feature(x) }, { } };			\
MODULE_DEVICE_TABLE(cpu, cpu_feature_match_ ## x);		\
static int __init cpu_feature_match_ ## x ## _init(void)	\
{								\
	if (!cpu_have_feature(cpu_feature(x)))			\
		return -ENODEV;					\
	return __initfunc();					\
}								\
module_init(cpu_feature_match_ ## x ## _init)
#include <linux/pm_qos.h>
#include <linux/sched/isolation.h>

#include "base.h"

static int cpu_subsys_match(struct device *dev, struct device_driver *drv)
{
	 
	if (acpi_driver_match_device(dev, drv))
		return 1;

	return 0;
}


struct bus_type cpu_subsys = {
	.name = "cpu",
	.dev_name = "cpu",
	.match = cpu_subsys_match,
};



/* Removed: cpu_device_create + __cpu_device_create + device_create_release - no callers */
/* Removed: CPU sysfs attribute groups - device_add_groups is a stub (no sysfs),
   so the .show callbacks were never dispatched. */

void __init cpu_dev_init(void)
{
	if (subsys_system_register(&cpu_subsys, NULL))
		panic("Failed to register CPU subsystem");
}
