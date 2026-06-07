/* Minimal rcupdate.h */
#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/preempt.h>
#include <linux/bottom_half.h>

void call_rcu(struct rcu_head *head, rcu_callback_t func);

static inline void __rcu_read_lock(void)
{
	preempt_disable();
}

static inline void __rcu_read_unlock(void)
{
	preempt_enable();
}

void rcu_init(void);

/* TINY_RCU only */
#include <asm/param.h>

extern void rcu_barrier(void);

void rcu_qs(void);

#define rcu_note_context_switch(preempt) \
	do { \
		rcu_qs(); \
	} while (0)

static inline void rcu_scheduler_starting(void) { }

#define rcu_dereference_raw(p) \
	((typeof(*p) __force __kernel *)READ_ONCE(p))

#define RCU_INITIALIZER(v) (typeof(*(v)) __force __rcu *)(v)

#define rcu_assign_pointer(p, v)					      \
do {									      \
	uintptr_t _r_a_p__v = (uintptr_t)(v);				      \
									      \
	if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL)	      \
		WRITE_ONCE((p), (typeof(p))(_r_a_p__v));		      \
	else								      \
		smp_store_release(&p, RCU_INITIALIZER((typeof(p))_r_a_p__v)); \
} while (0)

#define rcu_dereference_check(p, c) \
	((typeof(*p) __force __kernel *)READ_ONCE(p))

#define rcu_dereference_protected(p, c) \
	((typeof(*p) __force __kernel *)(p))

#define rcu_dereference(p) rcu_dereference_check(p, 0)

#define rcu_dereference_sched(p) \
	((typeof(*p) __force __kernel *)READ_ONCE(p))

static __always_inline void rcu_read_lock(void)
{
	__rcu_read_lock();
	__acquire(RCU);
}

static inline void rcu_read_unlock(void)
{
	__release(RCU);
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

#define RCU_INIT_POINTER(p, v) \
	do { \
		WRITE_ONCE(p, RCU_INITIALIZER(v)); \
	} while (0)

#define RCU_POINTER_INITIALIZER(p, v) \
		.p = RCU_INITIALIZER(v)

#define __is_kvfree_rcu_offset(offset) ((offset) < 4096)

#endif
