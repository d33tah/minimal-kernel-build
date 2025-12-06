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

# define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_swait_queue_head(&x->wait);
}

extern void wait_for_completion(struct completion *);
extern int wait_for_completion_interruptible(struct completion *x);
extern int wait_for_completion_killable(struct completion *x);
extern unsigned long wait_for_completion_timeout(struct completion *x,
						   unsigned long timeout);
extern bool completion_done(struct completion *x);

extern void complete(struct completion *);
extern void complete_all(struct completion *);

#endif
