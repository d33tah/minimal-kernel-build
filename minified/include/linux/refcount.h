
#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/limits.h>
#include <linux/spinlock_types.h>

typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }

enum refcount_saturation_type {
	REFCOUNT_ADD_OVF,
	REFCOUNT_ADD_UAF,
	REFCOUNT_SUB_UAF,
};

static inline void refcount_warn_saturate(refcount_t *r,
					  enum refcount_saturation_type t) {}

static inline void refcount_set(refcount_t *r, int n)
{
	atomic_set(&r->refs, n);
}

static inline void __refcount_add(int i, refcount_t *r, int *oldp)
{
	int old = atomic_fetch_add_relaxed(i, &r->refs);

	if (oldp)
		*oldp = old;

	if (unlikely(!old))
		refcount_warn_saturate(r, REFCOUNT_ADD_UAF);
	else if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_OVF);
}

static inline void __refcount_inc(refcount_t *r, int *oldp)
{
	__refcount_add(1, r, oldp);
}

static inline void refcount_inc(refcount_t *r)
{
	__refcount_inc(r, NULL);
}

static inline __must_check bool __refcount_sub_and_test(int i, refcount_t *r, int *oldp)
{
	int old = atomic_fetch_sub_release(i, &r->refs);

	if (oldp)
		*oldp = old;

	if (old == i) {
		smp_acquire__after_ctrl_dep();
		return true;
	}

	if (unlikely(old < 0 || old - i < 0))
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);

	return false;
}

static inline __must_check bool __refcount_dec_and_test(refcount_t *r, int *oldp)
{
	return __refcount_sub_and_test(1, r, oldp);
}

static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
	return __refcount_dec_and_test(r, NULL);
}

#endif
