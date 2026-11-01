/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Variant of atomic_t specialized for reference counts.
 *
 * The interface matches the atomic_t interface (to aid in porting) but only
 * provides the few functions one should use for reference counting.
 *
 * Saturation semantics
 * ====================
 *
 * refcount_t differs from atomic_t in that the counter saturates at
 * REFCOUNT_SATURATED and will not move once there. This avoids wrapping the
 * counter and causing 'spurious' use-after-free issues. In order to avoid the
 * cost associated with introducing cmpxchg() loops into all of the saturating
 * operations, we temporarily allow the counter to take on an unchecked value
 * and then explicitly set it to REFCOUNT_SATURATED on detecting that underflow
 * or overflow has occurred. Although this is racy when multiple threads
 * access the refcount concurrently, by placing REFCOUNT_SATURATED roughly
 * equidistant from 0 and INT_MAX we minimise the scope for error:
 *
 * 	                           INT_MAX     REFCOUNT_SATURATED   UINT_MAX
 *   0                          (0x7fff_ffff)    (0xc000_0000)    (0xffff_ffff)
 *   +--------------------------------+----------------+----------------+
 *                                     <---------- bad value! ---------->
 *
 * (in a signed view of the world, the "bad value" range corresponds to
 * a negative counter value).
 *
 * As an example, consider a refcount_inc() operation that causes the counter
 * to overflow:
 *
 * 	int old = atomic_fetch_add_relaxed(r);
 *	// old is INT_MAX, refcount now INT_MIN (0x8000_0000)
 *	if (old < 0)
 *		atomic_set(r, REFCOUNT_SATURATED);
 *
 * If another thread also performs a refcount_inc() operation between the two
 * atomic operations, then the count will continue to edge closer to 0. If it
 * reaches a value of 1 before /any/ of the threads reset it to the saturated
 * value, then a concurrent refcount_dec_and_test() may erroneously free the
 * underlying object.
 * Linux limits the maximum number of tasks to PID_MAX_LIMIT, which is currently
 * 0x400000 (and can't easily be raised in the future beyond FUTEX_TID_MASK).
 * With the current PID limit, if no batched refcounting operations are used and
 * the attacker can't repeatedly trigger kernel oopses in the middle of refcount
 * operations, this makes it impossible for a saturated refcount to leave the
 * saturation range, even if it is possible for multiple uses of the same
 * refcount to nest in the context of a single task:
 *
 *     (UINT_MAX+1-REFCOUNT_SATURATED) / PID_MAX_LIMIT =
 *     0x40000000 / 0x400000 = 0x100 = 256
 *
 * If hundreds of references are added/removed with a single refcounting
 * operation, it may potentially be possible to leave the saturation range; but
 * given the precise timing details involved with the round-robin scheduling of
 * each thread manipulating the refcount and the need to hit the race multiple
 * times in succession, there doesn't appear to be a practical avenue of attack
 * even if using refcount_add() operations with larger increments.
 *
 * Memory ordering
 * ===============
 *
 * Memory ordering rules are slightly relaxed wrt regular atomic_t functions
 * and provide only what is strictly required for refcounts.
 *
 * The increments are fully relaxed; these will not provide ordering. The
 * rationale is that whatever is used to obtain the object we're increasing the
 * reference count on will provide the ordering. For locked data structures,
 * its the lock acquire, for RCU/lockless data structures its the dependent
 * load.
 *
 * Do note that inc_not_zero() provides a control dependency which will order
 * future stores against the inc, this ensures we'll never modify the object
 * if we did not in fact acquire a reference.
 *
 * The decrements will provide release order, such that all the prior loads and
 * stores will be issued before, it also provides a control dependency, which
 * will order us against the subsequent free().
 *
 * The control dependency is against the load of the cmpxchg (ll/sc) that
 * succeeded. This means the stores aren't fully ordered, but this is fine
 * because the 1->0 transition indicates no concurrency.
 *
 * Note that the allocator is responsible for ordering things between free()
 * and alloc().
 *
 * The decrements dec_and_test() and sub_and_test() also provide acquire
 * ordering on success.
 *
 */

#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <linux/atomic.h>

/**
 * typedef refcount_t - variant of atomic_t specialized for reference counts
 * @refs: atomic_t counter field
 *
 * The counter saturates at REFCOUNT_SATURATED and will not move once
 * there. This avoids wrapping the counter and causing 'spurious'
 * use-after-free bugs.
 */
typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)                \
	{                               \
		.refs = ATOMIC_INIT(n), \
	}

/**
 * refcount_set - set a refcount's value
 * @r: the refcount
 * @n: value to which the refcount will be set
 */
static inline void refcount_set(refcount_t *r, int n)
{
	atomic_set(&r->refs, n);
}

static inline void __refcount_add(int i, refcount_t *r, int *oldp)
{
	int old = atomic_fetch_add_relaxed(i, &r->refs);

	if (oldp)
		*oldp = old;
}

static inline void __refcount_inc(refcount_t *r, int *oldp)
{
	__refcount_add(1, r, oldp);
}

/**
 * refcount_inc - increment a refcount
 * @r: the refcount to increment
 *
 * Similar to atomic_inc(), but will saturate at REFCOUNT_SATURATED and WARN.
 *
 * Provides no memory ordering, it is assumed the caller already has a
 * reference on the object.
 *
 * Will WARN if the refcount is 0, as this represents a possible use-after-free
 * condition.
 */
static inline void refcount_inc(refcount_t *r)
{
	__refcount_inc(r, NULL);
}

static inline __must_check bool __refcount_sub_and_test(int i, refcount_t *r,
							int *oldp)
{
	int old = atomic_fetch_sub_release(i, &r->refs);

	if (oldp)
		*oldp = old;

	if (old == i) {
		smp_acquire__after_ctrl_dep();
		return true;
	}

	return false;
}

static inline __must_check bool __refcount_dec_and_test(refcount_t *r,
							int *oldp)
{
	return __refcount_sub_and_test(1, r, oldp);
}

/**
 * refcount_dec_and_test - decrement a refcount and test if it is 0
 * @r: the refcount
 *
 * Similar to atomic_dec_and_test(), it will WARN on underflow and fail to
 * decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
	return __refcount_dec_and_test(r, NULL);
}

#endif /* _LINUX_REFCOUNT_H */
