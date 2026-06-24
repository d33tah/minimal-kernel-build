 
 
#ifndef _KERNEL_WORKQUEUE_INTERNAL_H
#define _KERNEL_WORKQUEUE_INTERNAL_H

#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/preempt.h>

/*
 * struct worker / struct worker_pool removed: the workqueue is fully stubbed
 * (kernel/workqueue.c runs work inline, no kworker threads), so no worker
 * descriptor is ever instantiated.  This header is retained only so that its
 * #includes (workqueue.h / kthread.h / preempt.h) stay pulled into the two
 * scheduler TUs (sched/core.c, sched/sched.h) that included it.
 */

#endif
