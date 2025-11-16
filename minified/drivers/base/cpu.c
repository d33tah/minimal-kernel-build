 
 

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
#include <linux/cpufeature.h>
#include <linux/tick.h>
#include <linux/pm_qos.h>
#include <linux/sched/isolation.h>

#include "base.h"

static DEFINE_PER_CPU(struct device *, cpu_sys_devices);

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


static const struct attribute_group *common_cpu_attr_groups[] = {
	NULL
};

static const struct attribute_group *hotplugable_cpu_attr_groups[] = {
	NULL
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

 
static ssize_t print_cpus_kernel_max(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", NR_CPUS - 1);
}
static DEVICE_ATTR(kernel_max, 0444, print_cpus_kernel_max, NULL);

 
unsigned int total_cpus;

static ssize_t print_cpus_offline(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int len = 0;
	cpumask_var_t offline;

	 
	if (!alloc_cpumask_var(&offline, GFP_KERNEL))
		return -ENOMEM;
	cpumask_andnot(offline, cpu_possible_mask, cpu_online_mask);
	len += sysfs_emit_at(buf, len, "%*pbl", cpumask_pr_args(offline));
	free_cpumask_var(offline);

	 
	if (total_cpus && nr_cpu_ids < total_cpus) {
		len += sysfs_emit_at(buf, len, ",");

		if (nr_cpu_ids == total_cpus-1)
			len += sysfs_emit_at(buf, len, "%u", nr_cpu_ids);
		else
			len += sysfs_emit_at(buf, len, "%u-%d",
					     nr_cpu_ids, total_cpus - 1);
	}

	len += sysfs_emit_at(buf, len, "\n");

	return len;
}
static DEVICE_ATTR(offline, 0444, print_cpus_offline, NULL);

static ssize_t print_cpus_isolated(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int len;
	cpumask_var_t isolated;

	if (!alloc_cpumask_var(&isolated, GFP_KERNEL))
		return -ENOMEM;

	cpumask_andnot(isolated, cpu_possible_mask,
		       housekeeping_cpumask(HK_TYPE_DOMAIN));
	len = sysfs_emit(buf, "%*pbl\n", cpumask_pr_args(isolated));

	free_cpumask_var(isolated);

	return len;
}
static DEVICE_ATTR(isolated, 0444, print_cpus_isolated, NULL);


static void cpu_device_release(struct device *dev)
{
	 
}

static ssize_t print_cpu_modalias(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	int len = 0;
	u32 i;

	len += sysfs_emit_at(buf, len,
			     "cpu:type:" CPU_FEATURE_TYPEFMT ":feature:",
			     CPU_FEATURE_TYPEVAL);

	for (i = 0; i < MAX_CPU_FEATURES; i++)
		if (cpu_have_feature(i)) {
			if (len + sizeof(",XXXX\n") >= PAGE_SIZE) {
				WARN(1, "CPU features overflow page\n");
				break;
			}
			len += sysfs_emit_at(buf, len, ",%04X", i);
		}
	len += sysfs_emit_at(buf, len, "\n");
	return len;
}

static int cpu_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf) {
		print_cpu_modalias(NULL, NULL, buf);
		add_uevent_var(env, "MODALIAS=%s", buf);
		kfree(buf);
	}
	return 0;
}

 
int register_cpu(struct cpu *cpu, int num)
{
	int error;

	cpu->node_id = cpu_to_node(num);
	memset(&cpu->dev, 0x00, sizeof(struct device));
	cpu->dev.id = num;
	cpu->dev.bus = &cpu_subsys;
	cpu->dev.release = cpu_device_release;
	cpu->dev.offline_disabled = !cpu->hotpluggable;
	cpu->dev.offline = !cpu_online(num);
	cpu->dev.of_node = of_get_cpu_node(num, NULL);
	cpu->dev.bus->uevent = cpu_uevent;
	cpu->dev.groups = common_cpu_attr_groups;
	if (cpu->hotpluggable)
		cpu->dev.groups = hotplugable_cpu_attr_groups;
	error = device_register(&cpu->dev);
	if (error) {
		put_device(&cpu->dev);
		return error;
	}

	per_cpu(cpu_sys_devices, num) = &cpu->dev;
	register_cpu_under_node(num, cpu_to_node(num));
	dev_pm_qos_expose_latency_limit(&cpu->dev,
					PM_QOS_RESUME_LATENCY_NO_CONSTRAINT);

	return 0;
}

struct device *get_cpu_device(unsigned int cpu)
{
	if (cpu < nr_cpu_ids && cpu_possible(cpu))
		return per_cpu(cpu_sys_devices, cpu);
	else
		return NULL;
}

static void device_create_release(struct device *dev)
{
	kfree(dev);
}

__printf(4, 0)
static struct device *
__cpu_device_create(struct device *parent, void *drvdata,
		    const struct attribute_group **groups,
		    const char *fmt, va_list args)
{
	struct device *dev = NULL;
	int retval = -ENOMEM;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		goto error;

	device_initialize(dev);
	dev->parent = parent;
	dev->groups = groups;
	dev->release = device_create_release;
	device_set_pm_not_required(dev);
	dev_set_drvdata(dev, drvdata);

	retval = kobject_set_name_vargs(&dev->kobj, fmt, args);
	if (retval)
		goto error;

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

struct device *cpu_device_create(struct device *parent, void *drvdata,
				 const struct attribute_group **groups,
				 const char *fmt, ...)
{
	va_list vargs;
	struct device *dev;

	va_start(vargs, fmt);
	dev = __cpu_device_create(parent, drvdata, groups, fmt, vargs);
	va_end(vargs);
	return dev;
}

static DEVICE_ATTR(modalias, 0444, print_cpu_modalias, NULL);

static struct attribute *cpu_root_attrs[] = {
	&cpu_attrs[0].attr.attr,
	&cpu_attrs[1].attr.attr,
	&cpu_attrs[2].attr.attr,
	&dev_attr_kernel_max.attr,
	&dev_attr_offline.attr,
	&dev_attr_isolated.attr,
	&dev_attr_modalias.attr,
	NULL
};

static const struct attribute_group cpu_root_attr_group = {
	.attrs = cpu_root_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&cpu_root_attr_group,
	NULL,
};

bool cpu_is_hotpluggable(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);
	return dev && container_of(dev, struct cpu, dev)->hotpluggable;
}


static void __init cpu_dev_register_generic(void)
{
}


ssize_t __weak cpu_show_meltdown(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spectre_v1(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spectre_v2(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spec_store_bypass(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_l1tf(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_mds(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_tsx_async_abort(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_itlb_multihit(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_srbds(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_mmio_stale_data(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

ssize_t __weak cpu_show_retbleed(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Not affected\n");
}

static DEVICE_ATTR(meltdown, 0444, cpu_show_meltdown, NULL);
static DEVICE_ATTR(spectre_v1, 0444, cpu_show_spectre_v1, NULL);
static DEVICE_ATTR(spectre_v2, 0444, cpu_show_spectre_v2, NULL);
static DEVICE_ATTR(spec_store_bypass, 0444, cpu_show_spec_store_bypass, NULL);
static DEVICE_ATTR(l1tf, 0444, cpu_show_l1tf, NULL);
static DEVICE_ATTR(mds, 0444, cpu_show_mds, NULL);
static DEVICE_ATTR(tsx_async_abort, 0444, cpu_show_tsx_async_abort, NULL);
static DEVICE_ATTR(itlb_multihit, 0444, cpu_show_itlb_multihit, NULL);
static DEVICE_ATTR(srbds, 0444, cpu_show_srbds, NULL);
static DEVICE_ATTR(mmio_stale_data, 0444, cpu_show_mmio_stale_data, NULL);
static DEVICE_ATTR(retbleed, 0444, cpu_show_retbleed, NULL);

static struct attribute *cpu_root_vulnerabilities_attrs[] = {
	&dev_attr_meltdown.attr,
	&dev_attr_spectre_v1.attr,
	&dev_attr_spectre_v2.attr,
	&dev_attr_spec_store_bypass.attr,
	&dev_attr_l1tf.attr,
	&dev_attr_mds.attr,
	&dev_attr_tsx_async_abort.attr,
	&dev_attr_itlb_multihit.attr,
	&dev_attr_srbds.attr,
	&dev_attr_mmio_stale_data.attr,
	&dev_attr_retbleed.attr,
	NULL
};

static const struct attribute_group cpu_root_vulnerabilities_group = {
	.name  = "vulnerabilities",
	.attrs = cpu_root_vulnerabilities_attrs,
};

static void __init cpu_register_vulnerabilities(void)
{
	if (sysfs_create_group(&cpu_subsys.dev_root->kobj,
			       &cpu_root_vulnerabilities_group))
		pr_err("Unable to register CPU vulnerabilities\n");
}


void __init cpu_dev_init(void)
{
	if (subsys_system_register(&cpu_subsys, cpu_root_attr_groups))
		panic("Failed to register CPU subsystem");

	cpu_dev_register_generic();
	cpu_register_vulnerabilities();
}
