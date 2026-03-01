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

struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
};

#endif
