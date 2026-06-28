#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/atomic.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/tick.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched/isolation.h>



#include "rcu.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcupdate."




void wakeme_after_rcu(struct rcu_head *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}

void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array)
{
	int i;
	int j;

	 
	for (i = 0; i < n; i++) {
		if (checktiny &&
		    (crcu_array[i] == call_rcu)) {
			might_sleep();
			continue;
		}
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i) {
			init_completion(&rs_array[i].completion);
			(crcu_array[i])(&rs_array[i].head, wakeme_after_rcu);
		}
	}

	 
	for (i = 0; i < n; i++) {
		if (checktiny &&
		    (crcu_array[i] == call_rcu))
			continue;
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i) {
			wait_for_completion(&rs_array[i].completion);
		}
	}
}



#define do_trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c) \
	do { } while (0)





