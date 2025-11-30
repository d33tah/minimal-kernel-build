 
#ifndef _LINUX_RCUWAIT_H_
#define _LINUX_RCUWAIT_H_

#include <linux/rcupdate.h>
#include <linux/sched/signal.h>

 
struct rcuwait {
	struct task_struct __rcu *task;
};

#define __RCUWAIT_INITIALIZER(name)		\
	{ .task = NULL, }

static inline void rcuwait_init(struct rcuwait *w)
{
	w->task = NULL;
}

 
static inline int rcuwait_active(struct rcuwait *w)
{
	return !!rcu_access_pointer(w->task);
}

extern int rcuwait_wake_up(struct rcuwait *w);

 

static inline void prepare_to_rcuwait(struct rcuwait *w)
{
	rcu_assign_pointer(w->task, current);
}

extern void finish_rcuwait(struct rcuwait *w);

#define rcuwait_wait_event(w, condition, state)				\
({									\
	int __ret = 0;							\
	prepare_to_rcuwait(w);						\
	for (;;) {							\
		 							\
		set_current_state(state);				\
		if (condition)						\
			break;						\
									\
		if (signal_pending_state(state, current)) {		\
			__ret = -EINTR;					\
			break;						\
		}							\
									\
		schedule();						\
	}								\
	finish_rcuwait(w);						\
	__ret;								\
})

#endif  
