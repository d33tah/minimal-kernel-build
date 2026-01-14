#ifndef _LINUX_SCHED_RT_H
#define _LINUX_SCHED_RT_H

#include <linux/sched.h>

struct task_struct;

static inline int rt_prio(int prio)
{
	if (unlikely(prio < MAX_RT_PRIO))
		return 1;
	return 0;
}

static inline int rt_task(struct task_struct *p)
{
	return rt_prio(p->prio);
}

# define rt_mutex_adjust_pi(p)		do { } while (0)


#define RR_TIMESLICE		(100 * HZ / 1000)

#endif  
