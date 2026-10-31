#ifndef __LINUX_LOCKREF_H
#define __LINUX_LOCKREF_H

#include <linux/spinlock.h>

struct lockref {
	struct {
		spinlock_t lock;
		int count;
	};
};

extern void lockref_get(struct lockref *);
extern void lockref_mark_dead(struct lockref *);

#endif
