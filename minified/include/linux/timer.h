#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/ktime.h>

struct timer_list {};

#define __TIMER_LOCKDEP_MAP_INITIALIZER(_kn)

/* TIMER_DEFERRABLE removed - 0-caller timer flag (not in any composite mask) */
#define TIMER_IRQSAFE 0x00200000

#define __TIMER_INITIALIZER(_function, _flags)                             \
	{                                                                  \
                                                                           \
		__TIMER_LOCKDEP_MAP_INITIALIZER(__FILE__                   \
						":" __stringify(__LINE__)) \
	}

#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)

/* timer_pending removed: 0-caller orphan (cascaded hlist_unhashed_lockless) */

#endif
