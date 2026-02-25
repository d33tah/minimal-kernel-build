/* Minimal workqueue.h */
#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

#define work_data_bits(work) ((unsigned long *)(&(work)->data))

enum {
	WORK_STRUCT_PENDING_BIT	= 0,
	WORK_STRUCT_STATIC	= 0,
	WORK_CPU_UNBOUND	= NR_CPUS,
	WORK_STRUCT_NO_POOL	= (unsigned long)
		(((1LU << (BITS_PER_LONG - 5 <= 31 ? BITS_PER_LONG - 5 : 31)) - 1) << 5),
};

struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
};

#define WORK_DATA_INIT()	ATOMIC_LONG_INIT((unsigned long)WORK_STRUCT_NO_POOL)
#define WORK_DATA_STATIC_INIT()	\
	ATOMIC_LONG_INIT((unsigned long)(WORK_STRUCT_NO_POOL | WORK_STRUCT_STATIC))


#define __WORK_INIT_LOCKDEP_MAP(n, k)

#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)

#define INIT_WORK(_work, _func)						\
	__INIT_WORK((_work), (_func), 0)

extern struct workqueue_struct *system_wq;

extern bool queue_work_on(int cpu, struct workqueue_struct *wq,
			struct work_struct *work);

static inline bool queue_work(struct workqueue_struct *wq,
			      struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}

static inline bool schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}

#endif
