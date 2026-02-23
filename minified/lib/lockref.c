#include <linux/spinlock.h>
#include <generated/bounds.h>
struct lockref {
	spinlock_t lock;
	int count;
};
#include <linux/bug.h>

#define CMPXCHG_LOOP(CODE, SUCCESS) \
	do {                        \
	} while (0)

void lockref_get(struct lockref *lockref)
{
	CMPXCHG_LOOP(new.count++;, return;);

	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}

int lockref_put_return(struct lockref *lockref)
{
	CMPXCHG_LOOP(new.count--; if (old.count <= 0) return -1;
		     , return new.count;);
	return -1;
}

void lockref_mark_dead(struct lockref *lockref)
{
	assert_spin_locked(&lockref->lock);
	lockref->count = -128;
}
