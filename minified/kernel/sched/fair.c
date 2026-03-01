#include "sched.h"

const struct sched_class fair_sched_class;

static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se = &p->se;

	if (se->on_rq)
		goto out;
	cfs_rq->nr_running++;
	if (cfs_rq->curr != se)
		cfs_rq->curr = se;
	se->on_rq = 1;
	cfs_rq->h_nr_running++;
out:
	rq->nr_running++;
}

static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se = &p->se;

	se->on_rq = 0;
	cfs_rq->nr_running--;
	if (cfs_rq->curr == se)
		cfs_rq->curr = NULL;
	cfs_rq->h_nr_running--;
	rq->nr_running--;
}

static void check_preempt_wakeup(struct rq *rq, struct task_struct *p,
				 int wake_flags)
{
}

struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev,
					struct rq_flags *rf)
{
	struct cfs_rq *cfs_rq = &rq->cfs;

	if (!sched_fair_runnable(rq))
		return NULL;

	if (prev)
		put_prev_task(rq, prev);

	return task_of(cfs_rq->curr);
}

static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	/* Nothing to do - curr stays set */
}

static void task_fork_fair(struct task_struct *p)
{
	/* Nothing to do for single-task kernel */
}

void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->curr = NULL;
}

DEFINE_SCHED_CLASS(fair) = {

	.enqueue_task = enqueue_task_fair,
	.dequeue_task = dequeue_task_fair,

	.check_preempt_curr = check_preempt_wakeup,

	.put_prev_task = put_prev_task_fair,

	.task_fork = task_fork_fair,

};
