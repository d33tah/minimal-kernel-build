
#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>

struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
void delayed_work_timer_fn(struct timer_list *t);

#define work_data_bits(work) ((unsigned long *)(&(work)->data))

enum {
	WORK_STRUCT_PENDING_BIT	= 0,	 
	WORK_STRUCT_INACTIVE_BIT= 1,	 
	WORK_STRUCT_PWQ_BIT	= 2,	 
	WORK_STRUCT_LINKED_BIT	= 3,	 
	WORK_STRUCT_COLOR_SHIFT	= 4,	 

	WORK_STRUCT_COLOR_BITS	= 4,

	WORK_STRUCT_PENDING	= 1 << WORK_STRUCT_PENDING_BIT,
	WORK_STRUCT_INACTIVE	= 1 << WORK_STRUCT_INACTIVE_BIT,
	WORK_STRUCT_PWQ		= 1 << WORK_STRUCT_PWQ_BIT,
	WORK_STRUCT_LINKED	= 1 << WORK_STRUCT_LINKED_BIT,
	WORK_STRUCT_STATIC	= 0,

	WORK_NR_COLORS		= (1 << WORK_STRUCT_COLOR_BITS),

	 
	WORK_CPU_UNBOUND	= NR_CPUS,

	 
	WORK_STRUCT_FLAG_BITS	= WORK_STRUCT_COLOR_SHIFT +
				  WORK_STRUCT_COLOR_BITS,

	 
	WORK_OFFQ_FLAG_BASE	= WORK_STRUCT_COLOR_SHIFT,

	__WORK_OFFQ_CANCELING	= WORK_OFFQ_FLAG_BASE,
	WORK_OFFQ_CANCELING	= (1 << __WORK_OFFQ_CANCELING),

	 
	WORK_OFFQ_FLAG_BITS	= 1,
	WORK_OFFQ_POOL_SHIFT	= WORK_OFFQ_FLAG_BASE + WORK_OFFQ_FLAG_BITS,
	WORK_OFFQ_LEFT		= BITS_PER_LONG - WORK_OFFQ_POOL_SHIFT,
	WORK_OFFQ_POOL_BITS	= WORK_OFFQ_LEFT <= 31 ? WORK_OFFQ_LEFT : 31,
	WORK_OFFQ_POOL_NONE	= (1LU << WORK_OFFQ_POOL_BITS) - 1,

	 
	WORK_STRUCT_FLAG_MASK	= (1UL << WORK_STRUCT_FLAG_BITS) - 1,
	WORK_STRUCT_WQ_DATA_MASK = ~WORK_STRUCT_FLAG_MASK,
	WORK_STRUCT_NO_POOL	= (unsigned long)WORK_OFFQ_POOL_NONE << WORK_OFFQ_POOL_SHIFT,

	 
	WORK_BUSY_PENDING	= 1 << 0,
	WORK_BUSY_RUNNING	= 1 << 1,

	 
	WORKER_DESC_LEN		= 24,
};

struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
};

#define WORK_DATA_INIT()	ATOMIC_LONG_INIT((unsigned long)WORK_STRUCT_NO_POOL)
#define WORK_DATA_STATIC_INIT()	\
	ATOMIC_LONG_INIT((unsigned long)(WORK_STRUCT_NO_POOL | WORK_STRUCT_STATIC))

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;

	 
	struct workqueue_struct *wq;
	int cpu;
};

struct rcu_work;

struct workqueue_attrs;

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

struct execute_work {
	struct work_struct work;
};

#define __WORK_INIT_LOCKDEP_MAP(n, k)

#define __WORK_INITIALIZER(n, f) {					\
	.data = WORK_DATA_STATIC_INIT(),				\
	.entry	= { &(n).entry, &(n).entry },				\
	.func = (f),							\
	__WORK_INIT_LOCKDEP_MAP(#n, &(n))				\
	}

#define __DELAYED_WORK_INITIALIZER(n, f, tflags) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),			\
	.timer = __TIMER_INITIALIZER(delayed_work_timer_fn,\
				     (tflags) | TIMER_IRQSAFE),		\
	}

#define DECLARE_WORK(n, f)						\
	struct work_struct n = __WORK_INITIALIZER(n, f)

#define DECLARE_DELAYED_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

#define DECLARE_DEFERRABLE_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, TIMER_DEFERRABLE)

static inline void __init_work(struct work_struct *work, int onstack) { }

#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		__init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)

#define INIT_WORK(_work, _func)						\
	__INIT_WORK((_work), (_func), 0)

#define INIT_WORK_ONSTACK(_work, _func)					\
	__INIT_WORK((_work), (_func), 1)

#define __INIT_DELAYED_WORK(_work, _func, _tflags)			\
	do {								\
		INIT_WORK(&(_work)->work, (_func));			\
		__init_timer(&(_work)->timer,				\
			     delayed_work_timer_fn,			\
			     (_tflags) | TIMER_IRQSAFE);		\
	} while (0)

#define __INIT_DELAYED_WORK_ONSTACK(_work, _func, _tflags)		\
	do {								\
		INIT_WORK_ONSTACK(&(_work)->work, (_func));		\
		__init_timer_on_stack(&(_work)->timer,			\
				      delayed_work_timer_fn,		\
				      (_tflags) | TIMER_IRQSAFE);	\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)					\
	__INIT_DELAYED_WORK(_work, _func, 0)

#define INIT_DELAYED_WORK_ONSTACK(_work, _func)				\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, 0)

#define INIT_DEFERRABLE_WORK(_work, _func)				\
	__INIT_DELAYED_WORK(_work, _func, TIMER_DEFERRABLE)

#define INIT_DEFERRABLE_WORK_ONSTACK(_work, _func)			\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, TIMER_DEFERRABLE)

#define INIT_RCU_WORK(_work, _func)					\
	INIT_WORK(&(_work)->work, (_func))

#define INIT_RCU_WORK_ONSTACK(_work, _func)				\
	INIT_WORK_ONSTACK(&(_work)->work, (_func))

#define work_pending(work) \
	test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))

#define delayed_work_pending(w) \
	work_pending(&(w)->work)

enum {
	WQ_UNBOUND		= 1 << 1,  
	WQ_FREEZABLE		= 1 << 2,  
	WQ_MEM_RECLAIM		= 1 << 3,  
	WQ_HIGHPRI		= 1 << 4,  
	WQ_CPU_INTENSIVE	= 1 << 5,  
	WQ_SYSFS		= 1 << 6,  

	 
	WQ_POWER_EFFICIENT	= 1 << 7,

	__WQ_DRAINING		= 1 << 16,  
	__WQ_ORDERED		= 1 << 17,  
	__WQ_LEGACY		= 1 << 18,  
	__WQ_ORDERED_EXPLICIT	= 1 << 19,  

	WQ_MAX_ACTIVE		= 512,	   
	WQ_MAX_UNBOUND_PER_CPU	= 4,	   
	WQ_DFL_ACTIVE		= WQ_MAX_ACTIVE / 2,
};

#define WQ_UNBOUND_MAX_ACTIVE	\
	max_t(int, WQ_MAX_ACTIVE, num_possible_cpus() * WQ_MAX_UNBOUND_PER_CPU)

extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;

__printf(1, 4) struct workqueue_struct *
alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...);

#define alloc_ordered_workqueue(fmt, flags, args...)			\
	alloc_workqueue(fmt, WQ_UNBOUND | __WQ_ORDERED |		\
			__WQ_ORDERED_EXPLICIT | (flags), 1, ##args)

#define create_workqueue(name)						\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_MEM_RECLAIM, 1, (name))
#define create_freezable_workqueue(name)				\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_FREEZABLE | WQ_UNBOUND |	\
			WQ_MEM_RECLAIM, 1, (name))
#define create_singlethread_workqueue(name)				\
	alloc_ordered_workqueue("%s", __WQ_LEGACY | WQ_MEM_RECLAIM, name)

extern void destroy_workqueue(struct workqueue_struct *wq);

struct workqueue_attrs *alloc_workqueue_attrs(void);
void free_workqueue_attrs(struct workqueue_attrs *attrs);
int apply_workqueue_attrs(struct workqueue_struct *wq,
			  const struct workqueue_attrs *attrs);
int workqueue_set_unbound_cpumask(cpumask_var_t cpumask);

extern bool queue_work_on(int cpu, struct workqueue_struct *wq,
			struct work_struct *work);
extern bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *work, unsigned long delay);
extern bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay);

extern void __flush_workqueue(struct workqueue_struct *wq);

int execute_in_process_context(work_func_t fn, struct execute_work *);

extern bool flush_work(struct work_struct *work);
extern bool cancel_work_sync(struct work_struct *work);

extern bool flush_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work_sync(struct delayed_work *dwork);

static inline bool queue_work(struct workqueue_struct *wq,
			      struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}

static inline bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	return queue_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}

static inline bool mod_delayed_work(struct workqueue_struct *wq,
				    struct delayed_work *dwork,
				    unsigned long delay)
{
	return mod_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}

static inline bool schedule_work_on(int cpu, struct work_struct *work)
{
	return queue_work_on(cpu, system_wq, work);
}

static inline bool schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}

extern void __warn_flushing_systemwide_wq(void)
	__compiletime_warning("Please avoid flushing system-wide workqueues.");

#define flush_scheduled_work()						\
({									\
	if (0)								\
		__warn_flushing_systemwide_wq();			\
	__flush_workqueue(system_wq);					\
})

#define flush_workqueue(wq)						\
({									\
	struct workqueue_struct *_wq = (wq);				\
									\
	if ((__builtin_constant_p(_wq == system_wq) &&			\
	     _wq == system_wq) ||					\
	    (__builtin_constant_p(_wq == system_highpri_wq) &&		\
	     _wq == system_highpri_wq) ||				\
	    (__builtin_constant_p(_wq == system_long_wq) &&		\
	     _wq == system_long_wq) ||					\
	    (__builtin_constant_p(_wq == system_unbound_wq) &&		\
	     _wq == system_unbound_wq) ||				\
	    (__builtin_constant_p(_wq == system_freezable_wq) &&	\
	     _wq == system_freezable_wq) ||				\
	    (__builtin_constant_p(_wq == system_power_efficient_wq) &&	\
	     _wq == system_power_efficient_wq) ||			\
	    (__builtin_constant_p(_wq == system_freezable_power_efficient_wq) && \
	     _wq == system_freezable_power_efficient_wq))		\
		__warn_flushing_systemwide_wq();			\
	__flush_workqueue(_wq);						\
})

static inline bool schedule_delayed_work(struct delayed_work *dwork,
					 unsigned long delay)
{
	return queue_delayed_work(system_wq, dwork, delay);
}

void __init workqueue_init_early(void);
void __init workqueue_init(void);

#endif
