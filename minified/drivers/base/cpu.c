
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



struct cpu_attr {
	struct device_attribute attr;
	const struct cpumask *const map;
};

static ssize_t show_cpus_attr(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct cpu_attr *ca = container_of(attr, struct cpu_attr, attr);

	return cpumap_print_to_pagebuf(true, buf, ca->map);
}

#define _CPU_ATTR(name, map) \
	{ __ATTR(name, 0444, show_cpus_attr, NULL), map }

static struct cpu_attr cpu_attrs[] = {
	_CPU_ATTR(online, &__cpu_online_mask),
	_CPU_ATTR(possible, &__cpu_possible_mask),
	_CPU_ATTR(present, &__cpu_present_mask),
};


/* Removed: cpu_device_create + __cpu_device_create + device_create_release - no callers */

/* Stub: CPU sysfs attributes minimized */
static struct attribute *cpu_root_attrs[] = {
	&cpu_attrs[0].attr.attr,
	&cpu_attrs[1].attr.attr,
	&cpu_attrs[2].attr.attr,
	NULL
};

static const struct attribute_group cpu_root_attr_group = {
	.attrs = cpu_root_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&cpu_root_attr_group,
	NULL,
};

void __init cpu_dev_init(void)
{
	if (subsys_system_register(&cpu_subsys, cpu_root_attr_groups))
		panic("Failed to register CPU subsystem");
}
