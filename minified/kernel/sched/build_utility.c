#include <linux/sched/signal.h>

#include "sched.h"

void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
			     struct lock_class_key *key)
{
	raw_spin_lock_init(&q->lock);
	INIT_LIST_HEAD(&q->task_list);
}
#include "wait_bit.c"
#include "wait.c"
