/* Minimal RT scheduler stub */
static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p, int f)
{
}
static void put_prev_task_rt(struct rq *rq, struct task_struct *p)
{
}

DEFINE_SCHED_CLASS(rt) = {
	.enqueue_task = enqueue_task_rt,
	.dequeue_task = dequeue_task_rt,
	.check_preempt_curr = check_preempt_curr_rt,
	.put_prev_task = put_prev_task_rt,
};

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
static void put_prev_task_dl(struct rq *rq, struct task_struct *p)
{
}
static void task_fork_dl(struct task_struct *p)
{
}
DEFINE_SCHED_CLASS(dl) = {
	.enqueue_task = enqueue_task_dl,
	.dequeue_task = dequeue_task_dl,
	.check_preempt_curr = check_preempt_curr_dl,
	.put_prev_task = put_prev_task_dl,
	.task_fork = task_fork_dl,
};
