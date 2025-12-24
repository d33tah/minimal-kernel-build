#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H
#include <linux/topology.h>
#include <linux/sched/idle.h>
#ifndef arch_scale_thermal_pressure
static __always_inline unsigned long arch_scale_thermal_pressure(int cpu) { return 0; }
#endif
#endif  
