#ifndef _LINUX_RCUWAIT_H_
#define _LINUX_RCUWAIT_H_
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
struct rcuwait { struct task_struct __rcu *task; };
/* __RCUWAIT_INITIALIZER, rcuwait_init removed - inlined at single call site */
extern int rcuwait_wake_up(struct rcuwait *w);
/* prepare_to_rcuwait, finish_rcuwait, rcuwait_wait_event removed - unused */
#endif  
