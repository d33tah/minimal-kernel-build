/* Minimal deadline scheduler stub */
/* sysctl_sched_dl_period_max, sysctl_sched_dl_period_min removed - unused */
/* __dl_update, __dl_sub, __dl_add, add_rq_bw, sub_rq_bw, add_running_bw, sub_running_bw,
   init_dl_bandwidth, init_dl_bw removed - unused */

void init_dl_rq(struct dl_rq *dl_rq)
{
	dl_rq->root = RB_ROOT_CACHED;
}

/* init_dl_task_timer, init_dl_inactive_task_timer removed - empty stubs */

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
}
static void dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
}
/* yield_task_dl removed - callback never called */
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
	/* .yield_task removed - callback never called */
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

/* __setparam_dl, __dl_clear_params removed - empty stubs */
