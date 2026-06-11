/* Minimal RT scheduler stub */

int sched_rr_timeslice = RR_TIMESLICE;
int sysctl_sched_rt_runtime = 950000;

void init_rt_rq(struct rt_rq *rt_rq)
{
}


static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int flags) { }
static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int flags) { }
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p, int flags) { }
static struct task_struct *pick_next_task_rt(struct rq *rq) { return NULL; }
static void put_prev_task_rt(struct rq *rq, struct task_struct *p) { }
static void set_next_task_rt(struct rq *rq, struct task_struct *p, bool first) { }
static void task_tick_rt(struct rq *rq, struct task_struct *p, int queued) { }
static void prio_changed_rt(struct rq *rq, struct task_struct *p, int oldprio) { }
static void switched_to_rt(struct rq *rq, struct task_struct *p) { }

DEFINE_SCHED_CLASS(rt) = {
	.enqueue_task		= enqueue_task_rt,
	.dequeue_task		= dequeue_task_rt,
	.check_preempt_curr	= check_preempt_curr_rt,
	.pick_next_task		= pick_next_task_rt,
	.put_prev_task		= put_prev_task_rt,
	.set_next_task          = set_next_task_rt,
	.task_tick		= task_tick_rt,
	.prio_changed		= prio_changed_rt,
	.switched_to		= switched_to_rt,
};
