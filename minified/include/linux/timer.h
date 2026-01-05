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

/* TIMER_CPUMASK, TIMER_MIGRATING, TIMER_DEFERRABLE removed - unused */
#define TIMER_IRQSAFE		0x00200000


#define __TIMER_INITIALIZER(_function, _flags) {		\
		.entry = { .next = TIMER_ENTRY_STATIC },	\
		.function = (_function),			\
		.flags = (_flags),				\
		__TIMER_LOCKDEP_MAP_INITIALIZER(		\
			__FILE__ ":" __stringify(__LINE__))	\
	}

#define DEFINE_TIMER(_name, _function)				\
	struct timer_list _name =				\
		__TIMER_INITIALIZER(_function, 0)

void init_timer_key(struct timer_list *timer,
		    void (*func)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key);

#define timer_setup(timer, callback, flags)			\
	init_timer_key((timer), (callback), (flags), NULL, NULL)

/* destroy_timer_on_stack removed - unused */

#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)

/* timer_pending removed - no callers */

extern int del_timer(struct timer_list * timer);
extern int mod_timer(struct timer_list *timer, unsigned long expires);


/* NEXT_TIMER_MAX_DELTA removed - unused */

# define del_timer_sync(t)		del_timer(t)

/* del_singleshot_timer_sync removed - unused */

extern void init_timers(void);

#define timers_prepare_cpu	NULL
#define timers_dead_cpu		NULL

#endif
