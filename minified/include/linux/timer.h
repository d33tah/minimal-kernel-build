#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct timer_list {
	 
	struct hlist_node	entry;
	unsigned long		expires;
	void			(*function)(struct timer_list *);
	u32			flags;

};

#define __TIMER_LOCKDEP_MAP_INITIALIZER(_kn)

#define TIMER_DEFERRABLE	0x00080000
#define TIMER_IRQSAFE		0x00200000


#define __TIMER_INITIALIZER(_function, _flags) {		\
		.entry = { .next = TIMER_ENTRY_STATIC },	\
		.function = (_function),			\
		.flags = (_flags),				\
		__TIMER_LOCKDEP_MAP_INITIALIZER(		\
			__FILE__ ":" __stringify(__LINE__))	\
	}

#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)

/* timer_pending removed: 0-caller orphan (cascaded hlist_unhashed_lockless) */

#define NEXT_TIMER_MAX_DELTA	((1UL << 30) - 1)

extern void init_timers(void);

#endif
