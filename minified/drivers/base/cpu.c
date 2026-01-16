
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/percpu.h>

#include <linux/mod_devicetable.h>
#include <linux/sched/isolation.h>

#include "base.h"

static int cpu_subsys_match(struct device *dev, struct device_driver *drv)
{
	return 0;
}

struct bus_type cpu_subsys = {
	.name = "cpu",
	.dev_name = "cpu",
	.match = cpu_subsys_match,
};
/* struct cpu_attr, show_cpus_attr removed - cpu_attrs removed */

/* device_create_release, __cpu_device_create, cpu_device_create removed - never called */
