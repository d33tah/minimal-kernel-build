#ifndef _LINUX_RCUWAIT_H_
#define _LINUX_RCUWAIT_H_
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
struct rcuwait { struct task_struct __rcu *task; };
/* __RCUWAIT_INITIALIZER removed - unused */
static inline void rcuwait_init(struct rcuwait *w) { w->task = NULL; }
extern int rcuwait_wake_up(struct rcuwait *w);
/* prepare_to_rcuwait removed - unused */
extern void finish_rcuwait(struct rcuwait *w);
/* rcuwait_wait_event removed - unused */
#endif  
