/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  Machine specific IO port address definition for generic.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef __LINUX_I8253_H
#define __LINUX_I8253_H

#include <linux/spinlock.h>

#define PIT_TICK_RATE 1193182ul

/* i8253A PIT registers */
#define PIT_MODE 0x43
#define PIT_CH0 0x40

#define PIT_LATCH ((PIT_TICK_RATE + HZ / 2) / HZ)

extern struct clock_event_device i8253_clockevent;
extern void clockevent_i8253_init(void);

#endif /* __LINUX_I8253_H */
