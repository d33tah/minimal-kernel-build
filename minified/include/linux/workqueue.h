/* Minimal workqueue.h - no actual workqueue, just struct definitions */
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
	WORK_STRUCT_NO_POOL	= (unsigned long)
		(((1LU << (BITS_PER_LONG - 5 <= 31 ? BITS_PER_LONG - 5 : 31)) - 1) << 5),
};

struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
};

#define WORK_DATA_INIT()	ATOMIC_LONG_INIT((unsigned long)WORK_STRUCT_NO_POOL)

#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)

#define INIT_WORK(_work, _func)						\
	__INIT_WORK((_work), (_func), 0)

#endif
