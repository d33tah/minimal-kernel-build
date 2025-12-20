/* Minimal rcupdate.h */
#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/preempt.h>
#include <linux/bottom_half.h>

#define ULONG_CMP_GE(a, b)	(ULONG_MAX / 2 >= (a) - (b))
#define ULONG_CMP_LT(a, b)	(ULONG_MAX / 2 < (a) - (b))
#define USHORT_CMP_GE(a, b)	(USHRT_MAX / 2 >= (unsigned short)((a) - (b)))
#define USHORT_CMP_LT(a, b)	(USHRT_MAX / 2 < (unsigned short)((a) - (b)))

void call_rcu(struct rcu_head *head, rcu_callback_t func);
void synchronize_rcu(void);

static inline void __rcu_read_lock(void)
{
	preempt_disable();
}

static inline void __rcu_read_unlock(void)
{
	preempt_enable();
}


void rcu_init(void);
extern int rcu_scheduler_active;
void rcu_sched_clock_irq(int user);
void rcu_report_dead(unsigned int cpu);
void rcutree_migrate_callbacks(int cpu);

static inline void rcu_init_tasks_generic(void) { }

static inline void rcu_init_nohz(void) { }
static inline void rcu_nocb_flush_deferred_wakeup(void) { }

#define rcu_tasks_qs(t, preempt) do { } while (0)
static inline void exit_tasks_rcu_start(void) { }
static inline void exit_tasks_rcu_finish(void) { }


#if defined(CONFIG_TREE_RCU)
#include <linux/rcutree.h>
#else
#include <asm/param.h>

unsigned long get_state_synchronize_rcu(void);
unsigned long start_poll_synchronize_rcu(void);

extern void rcu_barrier(void);

static inline void synchronize_rcu_expedited(void)
{
	synchronize_rcu();
}

extern void kvfree(const void *addr);


void rcu_qs(void);

static inline void rcu_softirq_qs(void)
{
	rcu_qs();
}

#define rcu_note_context_switch(preempt) \
	do { \
		rcu_qs(); \
		rcu_tasks_qs(current, (preempt)); \
	} while (0)

static inline void rcu_idle_enter(void) { }
static inline void rcu_idle_exit(void) { }
static inline void rcu_irq_enter(void) { }
static inline void rcu_irq_exit(void) { }
static inline void rcu_irq_exit_check_preempt(void) { }
static inline void exit_rcu(void) { }
void rcu_scheduler_starting(void);
static inline void rcu_end_inkernel_boot(void) { }
static inline bool rcu_is_watching(void) { return true; }
static inline void kfree_rcu_scheduler_running(void) { }

static inline void rcu_all_qs(void) { barrier(); }
#endif

static inline void init_rcu_head(struct rcu_head *head) { }
static inline void init_rcu_head_on_stack(struct rcu_head *head) { }
static inline void destroy_rcu_head_on_stack(struct rcu_head *head) { }

extern struct lockdep_map rcu_lock_map;
extern struct lockdep_map rcu_bh_lock_map;
extern struct lockdep_map rcu_sched_lock_map;
extern struct lockdep_map rcu_callback_map;


# define rcu_lock_acquire(a)		do { } while (0)
# define rcu_lock_release(a)		do { } while (0)

static inline int rcu_read_lock_held(void)
{
	return 1;
}


static inline int rcu_read_lock_sched_held(void)
{
	return !preemptible();
}

static inline int rcu_read_lock_any_held(void)
{
	return !preemptible();
}



#define RCU_LOCKDEP_WARN(c, s) do { } while (0 && (c))
#define rcu_sleep_check() do { } while (0)



#ifdef __CHECKER__
#define rcu_check_sparse(p, space) \
	((void)(((typeof(*p) space *)p) == p))
#else  
#define rcu_check_sparse(p, space)
#endif  

#define __rcu_access_pointer(p, local, space) \
({ \
	typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define __rcu_dereference_check(p, local, c, space) \
({ \
	  \
	typeof(*p) *local = (typeof(*p) *__force)READ_ONCE(p); \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_check() usage"); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define __rcu_dereference_protected(p, local, c, space) \
({ \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_protected() usage"); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(p)); \
})
#define __rcu_dereference_raw(p, local) \
({ \
	  \
	typeof(p) local = READ_ONCE(p); \
	((typeof(*p) __force __kernel *)(local)); \
})
#define rcu_dereference_raw(p) __rcu_dereference_raw(p, __UNIQUE_ID(rcu))

#define RCU_INITIALIZER(v) (typeof(*(v)) __force __rcu *)(v)

#define rcu_assign_pointer(p, v)					      \
do {									      \
	uintptr_t _r_a_p__v = (uintptr_t)(v);				      \
	rcu_check_sparse(p, __rcu);					      \
									      \
	if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL)	      \
		WRITE_ONCE((p), (typeof(p))(_r_a_p__v));		      \
	else								      \
		smp_store_release(&p, RCU_INITIALIZER((typeof(p))_r_a_p__v)); \
} while (0)


#define rcu_access_pointer(p) __rcu_access_pointer((p), __UNIQUE_ID(rcu), __rcu)

#define rcu_dereference_check(p, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_held(), __rcu)


#define rcu_dereference_sched_check(p, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || rcu_read_lock_sched_held(), \
				__rcu)


#define rcu_dereference_protected(p, c) \
	__rcu_dereference_protected((p), __UNIQUE_ID(rcu), (c), __rcu)


#define rcu_dereference(p) rcu_dereference_check(p, 0)


#define rcu_dereference_sched(p) rcu_dereference_sched_check(p, 0)

static __always_inline void rcu_read_lock(void)
{
	__rcu_read_lock();
	__acquire(RCU);
	rcu_lock_acquire(&rcu_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock() used illegally while idle");
}


static inline void rcu_read_unlock(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock() used illegally while idle");
	__release(RCU);
	__rcu_read_unlock();
	rcu_lock_release(&rcu_lock_map);  
}

static inline void rcu_read_lock_sched(void)
{
	preempt_disable();
	__acquire(RCU_SCHED);
	rcu_lock_acquire(&rcu_sched_lock_map);
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_lock_sched() used illegally while idle");
}

static inline void rcu_read_unlock_sched(void)
{
	RCU_LOCKDEP_WARN(!rcu_is_watching(),
			 "rcu_read_unlock_sched() used illegally while idle");
	rcu_lock_release(&rcu_sched_lock_map);
	__release(RCU_SCHED);
	preempt_enable();
}

#define RCU_INIT_POINTER(p, v) \
	do { \
		rcu_check_sparse(p, __rcu); \
		WRITE_ONCE(p, RCU_INITIALIZER(v)); \
	} while (0)

#define RCU_POINTER_INITIALIZER(p, v) \
		.p = RCU_INITIALIZER(v)

#define __is_kvfree_rcu_offset(offset) ((offset) < 4096)


#define smp_mb__after_unlock_lock()	do { } while (0)

#endif
