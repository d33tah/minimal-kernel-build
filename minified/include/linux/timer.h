#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/list.h>
/* ktime.h inlined */
/* jiffies.h inlined */
#include <linux/minmax.h>
#include <linux/timex.h>
#include <asm/param.h>
#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)
#ifndef __jiffy_arch_data
#define __jiffy_arch_data
#endif
extern u64 __cacheline_aligned_in_smp jiffies_64;
extern unsigned long volatile __cacheline_aligned_in_smp __jiffy_arch_data jiffies;
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))
#include <asm/bug.h>
typedef s64 ktime_t;
#include <linux/stddef.h>
#include <linux/stringify.h>

struct timer_list {

	struct hlist_node	entry;
	void			(*function)(struct timer_list *);

};

#endif
