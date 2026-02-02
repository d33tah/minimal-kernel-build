/* Minimal RT scheduler stub */

unsigned int sysctl_sched_rt_period = 1000000;
int sysctl_sched_rt_runtime = 950000;
struct rt_bandwidth def_rt_bandwidth;

void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime)
{
	/* rt_runtime, rt_runtime_lock removed - write-only, never read */
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
	/* rt_runtime, rt_runtime_lock removed - write-only, never read */
}

/* Stub functions - RT scheduler not used */
static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static struct task_struct *pick_next_task_rt(struct rq *rq)
{
	return NULL;
}
static void put_prev_task_rt(struct rq *rq, struct task_struct *p)
{
}
static void set_next_task_rt(struct rq *rq, struct task_struct *p, bool f)
{
}
static void task_tick_rt(struct rq *rq, struct task_struct *p, int q)
{
}
/* get_rr_interval_rt removed - callback never called (~4 LOC) */
static void prio_changed_rt(struct rq *rq, struct task_struct *p, int o)
{
}
static void switched_to_rt(struct rq *rq, struct task_struct *p)
{
}
static void update_curr_rt(struct rq *rq)
{
}

DEFINE_SCHED_CLASS(rt) = {
	.enqueue_task = enqueue_task_rt,
	.dequeue_task = dequeue_task_rt,
	.check_preempt_curr = check_preempt_curr_rt,
	.pick_next_task = pick_next_task_rt,
	.put_prev_task = put_prev_task_rt,
	.set_next_task = set_next_task_rt,
	.task_tick = task_tick_rt,
	.prio_changed = prio_changed_rt,
	.switched_to = switched_to_rt,
	.update_curr = update_curr_rt,
};

/* Merged from deadline.c */
void init_dl_rq(struct dl_rq *dl_rq)
{
	/* All fields removed - dl_rq never read */
}
static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
}
static void dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
}
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p,
				  int flags)
{
}
static struct task_struct *pick_next_task_dl(struct rq *rq)
{
	return NULL;
}
static void put_prev_task_dl(struct rq *rq, struct task_struct *p)
{
}
static void set_next_task_dl(struct rq *rq, struct task_struct *p, bool first)
{
}
static void task_tick_dl(struct rq *rq, struct task_struct *p, int queued)
{
}
static void task_fork_dl(struct task_struct *p)
{
}
static void prio_changed_dl(struct rq *rq, struct task_struct *p, int oldprio)
{
}
static void switched_from_dl(struct rq *rq, struct task_struct *p)
{
}
static void switched_to_dl(struct rq *rq, struct task_struct *p)
{
}
static void update_curr_dl(struct rq *rq)
{
}
DEFINE_SCHED_CLASS(dl) = {
	.enqueue_task = enqueue_task_dl,
	.dequeue_task = dequeue_task_dl,
	.check_preempt_curr = check_preempt_curr_dl,
	.pick_next_task = pick_next_task_dl,
	.put_prev_task = put_prev_task_dl,
	.set_next_task = set_next_task_dl,
	.task_tick = task_tick_dl,
	.task_fork = task_fork_dl,
	.prio_changed = prio_changed_dl,
	.switched_from = switched_from_dl,
	.switched_to = switched_to_dl,
	.update_curr = update_curr_dl,
};
