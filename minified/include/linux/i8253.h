#ifndef __LINUX_I8253_H
#define __LINUX_I8253_H

#include <linux/param.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#define PIT_MODE	0x43
#define PIT_CH0		0x40
#define PIT_CH2		0x42

#define PIT_LATCH	((PIT_TICK_RATE + HZ/2) / HZ)

extern struct clock_event_device i8253_clockevent;
extern void clockevent_i8253_init(bool oneshot);

#endif  
