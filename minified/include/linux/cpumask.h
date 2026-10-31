#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/threads.h>
#include <linux/bitmap.h>

/*
 * NR_CPUS is fixed to 1 in this build (CONFIG_NR_CPUS=1, unconditional in
 * autoconf.h + threads.h), so `#if NR_CPUS == 1` is always true; the
 * multi-CPU `#else extern unsigned int nr_cpu_ids;` arm was statically dead
 * (no out-of-line definition of nr_cpu_ids exists anyway). Emit the fixed
 * one-CPU value unconditionally.
 */
#define nr_cpu_ids 1U

/* NR_CPUS == 1 - simplified */
#define num_possible_cpus() 1U
#define num_present_cpus() 1U

#endif
