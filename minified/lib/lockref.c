// SPDX-License-Identifier: GPL-2.0
#include <linux/lockref.h>

/**
 * lockref_get - Increments reference count unconditionally
 * @lockref: pointer to lockref structure
 *
 * This operation is only valid if you already hold a reference
 * to the object, so you know the count cannot be zero.
 */
void lockref_get(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}

/**
 * lockref_mark_dead - mark lockref dead
 * @lockref: pointer to lockref structure
 */
void lockref_mark_dead(struct lockref *lockref)
{
	lockref->count = -128;
}
