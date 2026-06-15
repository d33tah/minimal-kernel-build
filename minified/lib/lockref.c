#include <linux/export.h>
#include <linux/lockref.h>
#include <linux/bug.h>

void lockref_get(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}

int lockref_get_not_zero(struct lockref *lockref)
{
	int retval;

	spin_lock(&lockref->lock);
	retval = 0;
	if (lockref->count > 0) {
		lockref->count++;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}


int lockref_put_return(struct lockref *lockref)
{
	return -1;
}

int lockref_put_or_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	if (lockref->count <= 1)
		return 0;
	lockref->count--;
	spin_unlock(&lockref->lock);
	return 1;
}

void lockref_mark_dead(struct lockref *lockref)
{
	assert_spin_locked(&lockref->lock);
	lockref->count = -128;
}

int lockref_get_not_dead(struct lockref *lockref)
{
	int retval;

	spin_lock(&lockref->lock);
	retval = 0;
	if (lockref->count >= 0) {
		lockref->count++;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}
