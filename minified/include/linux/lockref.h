#ifndef __LINUX_LOCKREF_H
#define __LINUX_LOCKREF_H


#include <linux/spinlock.h>
#include <generated/bounds.h>

/* No USE_CMPXCHG_LOCKREF - SMP is disabled */
struct lockref {
	spinlock_t lock;
	int count;
};

extern void lockref_get(struct lockref *);
extern int lockref_put_return(struct lockref *);
extern int lockref_get_not_zero(struct lockref *);
extern int lockref_put_or_lock(struct lockref *);

extern void lockref_mark_dead(struct lockref *);
extern int lockref_get_not_dead(struct lockref *);

#endif  
