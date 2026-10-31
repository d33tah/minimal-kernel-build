/* Minimal rcupdate.h */
#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#include <linux/preempt.h>

static inline void __rcu_read_lock(void)
{
	preempt_disable();
}

static inline void __rcu_read_unlock(void)
{
	preempt_enable();
}

static inline int rcu_read_lock_held(void)
{
	return 1;
}

static inline int rcu_read_lock_sched_held(void)
{
	return !preemptible();
}

#define __rcu_access_pointer(p, local, space)                          \
	({                                                             \
		typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
		((typeof(*p) __force __kernel *)(local));              \
	})
#define __rcu_dereference_check(p, local, c, space)                    \
	({                                                             \
		typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
		((typeof(*p) __force __kernel *)(local));              \
	})
#define __rcu_dereference_protected(p, local, c, space) \
	({ ((typeof(*p) __force __kernel *)(p)); })
#define __rcu_dereference_raw(p, local)                   \
	({                                                \
		typeof(p) local = READ_ONCE(p);           \
		((typeof(*p) __force __kernel *)(local)); \
	})
#define rcu_dereference_raw(p) __rcu_dereference_raw(p, __UNIQUE_ID(rcu))

#define RCU_INITIALIZER(v) (typeof(*(v)) __force __rcu *)(v)

#define rcu_assign_pointer(p, v)                                               \
	do {                                                                   \
		uintptr_t _r_a_p__v = (uintptr_t)(v);                          \
		if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL) \
			WRITE_ONCE((p), (typeof(p))(_r_a_p__v));               \
		else                                                           \
			smp_store_release(                                     \
				&p, RCU_INITIALIZER((typeof(p))_r_a_p__v));    \
	} while (0)

#define rcu_access_pointer(p) __rcu_access_pointer((p), __UNIQUE_ID(rcu), __rcu)

#define rcu_dereference_check(p, c)                    \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_held(), __rcu)

#define rcu_dereference_sched_check(p, c)              \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_sched_held(), __rcu)

#define rcu_dereference_protected(p, c) \
	__rcu_dereference_protected((p), __UNIQUE_ID(rcu), (c), __rcu)

#define rcu_dereference(p) rcu_dereference_check(p, 0)

#define rcu_dereference_sched(p) rcu_dereference_sched_check(p, 0)

static __always_inline void rcu_read_lock(void)
{
	__rcu_read_lock();
}

static inline void rcu_read_unlock(void)
{
	__rcu_read_unlock();
}

static inline void rcu_read_lock_sched(void)
{
	preempt_disable();
}

static inline void rcu_read_unlock_sched(void)
{
	preempt_enable();
}

#define RCU_INIT_POINTER(p, v)                     \
	do {                                       \
		WRITE_ONCE(p, RCU_INITIALIZER(v)); \
	} while (0)

#define RCU_POINTER_INITIALIZER(p, v) .p = RCU_INITIALIZER(v)

#endif
