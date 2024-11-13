/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sched_clock.h: support for extending counters to full 64-bit ns counter
 */
#ifndef LINUX_SCHED_CLOCK
#define LINUX_SCHED_CLOCK

#include <linux/types.h>

static inline void generic_sched_clock_init(void) { }

static inline void sched_clock_register(u64 (*read)(void), int bits,
					unsigned long rate)
{
}

#endif
