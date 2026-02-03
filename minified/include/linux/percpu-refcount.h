
#ifndef _LINUX_PERCPU_REFCOUNT_H
#define _LINUX_PERCPU_REFCOUNT_H

#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/gfp.h>

struct percpu_ref;
typedef void (percpu_ref_func_t)(struct percpu_ref *);

enum {
	__PERCPU_REF_ATOMIC	= 1LU << 0,	 
	__PERCPU_REF_DEAD	= 1LU << 1,	 
	__PERCPU_REF_ATOMIC_DEAD = __PERCPU_REF_ATOMIC | __PERCPU_REF_DEAD,

	__PERCPU_REF_FLAG_BITS	= 2,
};

/* PERCPU_REF_INIT_* enum removed - never used */

struct percpu_ref_data {
	atomic_long_t		count;
	percpu_ref_func_t	*release;
	percpu_ref_func_t	*confirm_switch;
	bool			force_atomic:1;
	bool			allow_reinit:1;
	struct rcu_head		rcu;
	struct percpu_ref	*ref;
};

struct percpu_ref {
	 
	unsigned long		percpu_count_ptr;

	 
	struct percpu_ref_data  *data;
};

/* percpu_ref_* function declarations removed - never defined or called */

static inline bool __ref_is_percpu(struct percpu_ref *ref,
					  unsigned long __percpu **percpu_countp)
{
	unsigned long percpu_ptr;

	 
	percpu_ptr = READ_ONCE(ref->percpu_count_ptr);

	 
	if (unlikely(percpu_ptr & __PERCPU_REF_ATOMIC_DEAD))
		return false;

	*percpu_countp = (unsigned long __percpu *)percpu_ptr;
	return true;
}

static inline void percpu_ref_put_many(struct percpu_ref *ref, unsigned long nr)
{
	unsigned long __percpu *percpu_count;

	rcu_read_lock();

	if (__ref_is_percpu(ref, &percpu_count))
		this_cpu_sub(*percpu_count, nr);
	else if (unlikely(atomic_long_sub_and_test(nr, &ref->data->count)))
		ref->data->release(ref);

	rcu_read_unlock();
}

static inline void percpu_ref_put(struct percpu_ref *ref)
{
	percpu_ref_put_many(ref, 1);
}

#endif
