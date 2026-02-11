#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H


#include <linux/swait.h>

struct completion {
	unsigned int done;
	struct swait_queue_head wait;
};

#define COMPLETION_INITIALIZER(work) \
	{ 0, __SWAIT_QUEUE_HEAD_INITIALIZER((work).wait) }

#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)

/* DECLARE_COMPLETION_ONSTACK removed - no callers */

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_swait_queue_head(&x->wait);
}

/* Stubs from kernel/sched/completion.c - inlined */
static inline void wait_for_completion(struct completion *x) { }
/* wait_for_completion_killable removed - never called */
static inline void complete(struct completion *x) { }

#endif
