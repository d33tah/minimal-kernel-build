/* Minimal RT scheduler stub */

int sched_rr_timeslice = RR_TIMESLICE;
int sysctl_sched_rt_runtime = 950000;

void init_rt_rq(struct rt_rq *rt_rq)
{
}


/*
 * No task is ever RT on this build (init/kthreads run SCHED_NORMAL/IDLE only),
 * so the rt runqueue is never populated. Of the sched_class members, only
 * pick_next_task is dispatched (via for_each_class in __pick_next_task, where
 * it returns NULL because the rt_rq is empty). Every other member
 * (enqueue/dequeue/check_preempt_curr/put_prev_task/set_next_task/task_tick/
 * prio_changed/switched_to) is only ever invoked for a task already in the rt
 * class, which never happens, so those bodies were dead and are dropped (the
 * struct members default to NULL and are never dereferenced).
 */
static struct task_struct *pick_next_task_rt(struct rq *rq) { return NULL; }

DEFINE_SCHED_CLASS(rt) = {
	.pick_next_task		= pick_next_task_rt,
};
