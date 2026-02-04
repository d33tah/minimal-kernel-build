#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct timer_list {

	struct hlist_node	entry;
	/* expires removed - never accessed (timers are stubbed) */
	void			(*function)(struct timer_list *);
	/* flags removed - write-only (never read) */

};

#define __TIMER_LOCKDEP_MAP_INITIALIZER(_kn)

/* TIMER_CPUMASK, TIMER_MIGRATING, TIMER_DEFERRABLE removed - unused */
#define TIMER_IRQSAFE		0x00200000


#define __TIMER_INITIALIZER(_function, _flags) {		\
		.entry = { .next = TIMER_ENTRY_STATIC },	\
		.function = (_function),			\
		/* .flags removed - write-only */		\
		__TIMER_LOCKDEP_MAP_INITIALIZER(		\
			__FILE__ ":" __stringify(__LINE__))	\
	}

/* DEFINE_TIMER removed - never used */

void init_timer_key(struct timer_list *timer,
		    void (*func)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key);

#define timer_setup(timer, callback, flags)			\
	init_timer_key((timer), (callback), (flags), NULL, NULL)

/* destroy_timer_on_stack removed - unused */

#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)

/* timer_pending, del_timer, mod_timer, del_timer_sync removed - no callers */

/* init_timers removed - was empty stub */
/* timers_prepare_cpu, timers_dead_cpu removed - unused */

#endif
