/* Minimal RT scheduler stub */

int sched_rr_timeslice = RR_TIMESLICE;
unsigned int sysctl_sched_rt_period = 1000000;
int sysctl_sched_rt_runtime = 950000;
struct rt_bandwidth def_rt_bandwidth;

void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime)
{
	rt_b->rt_runtime = runtime;
	raw_spin_lock_init(&rt_b->rt_runtime_lock);
}

void init_rt_rq(struct rt_rq *rt_rq)
{
	struct rt_prio_array *array;
	int i;

	array = &rt_rq->active;
	for (i = 0; i < MAX_RT_PRIO; i++) {
		INIT_LIST_HEAD(array->queue + i);
		__clear_bit(i, array->bitmap);
	}
	__set_bit(MAX_RT_PRIO, array->bitmap);
	rt_rq->rt_queued = 0;
	rt_rq->rt_time = 0;
	rt_rq->rt_throttled = 0;
	rt_rq->rt_runtime = 0;
	raw_spin_lock_init(&rt_rq->rt_runtime_lock);
}

void unregister_rt_sched_group(struct task_group *tg) { }
void free_rt_sched_group(struct task_group *tg) { }
int alloc_rt_sched_group(struct task_group *tg, struct task_group *parent) { return 1; }

static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int flags) { }
static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int flags) { }
static void yield_task_rt(struct rq *rq) { }
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p, int flags) { }
static struct task_struct *pick_next_task_rt(struct rq *rq) { return NULL; }
static void put_prev_task_rt(struct rq *rq, struct task_struct *p) { }
static void set_next_task_rt(struct rq *rq, struct task_struct *p, bool first) { }
static void task_tick_rt(struct rq *rq, struct task_struct *p, int queued) { }
static unsigned int get_rr_interval_rt(struct rq *rq, struct task_struct *task) { return 0; }
static void prio_changed_rt(struct rq *rq, struct task_struct *p, int oldprio) { }
static void switched_to_rt(struct rq *rq, struct task_struct *p) { }
static void update_curr_rt(struct rq *rq) { }

DEFINE_SCHED_CLASS(rt) = {
	.enqueue_task		= enqueue_task_rt,
	.dequeue_task		= dequeue_task_rt,
	.yield_task		= yield_task_rt,
	.check_preempt_curr	= check_preempt_curr_rt,
	.pick_next_task		= pick_next_task_rt,
	.put_prev_task		= put_prev_task_rt,
	.set_next_task          = set_next_task_rt,
	.task_tick		= task_tick_rt,
	.get_rr_interval	= get_rr_interval_rt,
	.prio_changed		= prio_changed_rt,
	.switched_to		= switched_to_rt,
	.update_curr		= update_curr_rt,
};
