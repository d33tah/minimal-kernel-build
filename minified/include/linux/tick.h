#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H
#include <linux/clockchips.h>
#include <linux/cpumask.h>
extern void __init tick_init(void);
enum tick_dep_bits { TICK_DEP_BIT_CLOCK_UNSTABLE = 3, };
/* tick_irq_enter, tick_nohz_*, tick_nohz_enabled removed - never called */
#endif
