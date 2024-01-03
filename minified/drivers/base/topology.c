// SPDX-License-Identifier: GPL-2.0+
/*
 * driver/base/topology.c - Populate sysfs with cpu topology information
 *
 * Written by: Zhang Yanmin, Intel Corporation
 *
 * Copyright (C) 2006, Intel Corp.
 *
 * All rights reserved.
 */
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/topology.h>

#define define_id_show_func(name,fmt) static ssize_t name ##_show(struct device *dev, struct device_attribute *attr, char *buf) { return sysfs_emit(buf, fmt "\n", topology_ ##name(dev->id)); }

#define define_siblings_read_func(name,mask) static ssize_t name ##_read(struct file *file, struct kobject *kobj, struct bin_attribute *attr, char *buf, loff_t off, size_t count) { struct device *dev = kobj_to_dev(kobj); return cpumap_print_bitmask_to_buf(buf, topology_ ##mask(dev->id), off, count); } static ssize_t name ##_list_read(struct file *file, struct kobject *kobj, struct bin_attribute *attr, char *buf, loff_t off, size_t count) { struct device *dev = kobj_to_dev(kobj); return cpumap_print_list_to_buf(buf, topology_ ##mask(dev->id), off, count); }

define_id_show_func(physical_package_id, "%d");
static DEVICE_ATTR_RO(physical_package_id);



define_id_show_func(core_id, "%d");
static DEVICE_ATTR_RO(core_id);

define_id_show_func(ppin, "0x%llx");
static DEVICE_ATTR_ADMIN_RO(ppin);

define_siblings_read_func(thread_siblings, sibling_cpumask);
static BIN_ATTR_RO(thread_siblings, 0);
static BIN_ATTR_RO(thread_siblings_list, 0);

define_siblings_read_func(core_cpus, sibling_cpumask);
static BIN_ATTR_RO(core_cpus, 0);
static BIN_ATTR_RO(core_cpus_list, 0);

define_siblings_read_func(core_siblings, core_cpumask);
static BIN_ATTR_RO(core_siblings, 0);
static BIN_ATTR_RO(core_siblings_list, 0);



define_siblings_read_func(package_cpus, core_cpumask);
static BIN_ATTR_RO(package_cpus, 0);
static BIN_ATTR_RO(package_cpus_list, 0);



static struct bin_attribute *bin_attrs[] = {
	&bin_attr_core_cpus,
	&bin_attr_core_cpus_list,
	&bin_attr_thread_siblings,
	&bin_attr_thread_siblings_list,
	&bin_attr_core_siblings,
	&bin_attr_core_siblings_list,
	&bin_attr_package_cpus,
	&bin_attr_package_cpus_list,
	NULL
};

static struct attribute *default_attrs[] = {
	&dev_attr_physical_package_id.attr,
	&dev_attr_core_id.attr,
	&dev_attr_ppin.attr,
	NULL
};

static umode_t topology_is_visible(struct kobject *kobj,
				   struct attribute *attr, int unused)
{
	if (attr == &dev_attr_ppin.attr && !topology_ppin(kobj_to_dev(kobj)->id))
		return 0;

	return attr->mode;
}

static const struct attribute_group topology_attr_group = {
	.attrs = default_attrs,
	.bin_attrs = bin_attrs,
	.is_visible = topology_is_visible,
	.name = "topology"
};

/* Add/Remove cpu_topology interface for CPU device */
static int topology_add_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	return sysfs_create_group(&dev->kobj, &topology_attr_group);
}

static int topology_remove_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	sysfs_remove_group(&dev->kobj, &topology_attr_group);
	return 0;
}

static int __init topology_sysfs_init(void)
{
	return cpuhp_setup_state(CPUHP_TOPOLOGY_PREPARE,
				 "base/topology:prepare", topology_add_dev,
				 topology_remove_dev);
}

device_initcall(topology_sysfs_init);
