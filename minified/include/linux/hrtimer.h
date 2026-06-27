#ifndef _LINUX_HRTIMER_H
#define _LINUX_HRTIMER_H


#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/seqlock.h>
#include <linux/timer.h>

/* hrtimer structs/enums/macros + tick_cpu_device DECLARE_PER_CPU dropped:
 * hrtimer.c is gone, all hrtimer types are unreferenced tree-wide, and
 * tick_cpu_device is declared in tick-internal.h. Includes kept for the
 * transitive headers includers rely on. */

#endif
