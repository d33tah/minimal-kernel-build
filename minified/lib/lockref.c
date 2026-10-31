#include <linux/lockref.h>

void lockref_get(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}

void lockref_mark_dead(struct lockref *lockref)
{
	lockref->count = -128;
}
