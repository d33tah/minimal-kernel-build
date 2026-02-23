#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/list.h>
/* ktime.h inlined */
#include <linux/jiffies.h>
#include <asm/bug.h>
typedef s64 ktime_t;
#include <linux/stddef.h>
#include <linux/stringify.h>

struct timer_list {

	struct hlist_node	entry;
	void			(*function)(struct timer_list *);

};

#endif
