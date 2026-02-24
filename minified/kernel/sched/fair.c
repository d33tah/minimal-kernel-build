#include <linux/jiffies.h>
#include "sched.h"

static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
}

static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
}

const struct sched_class fair_sched_class;

#define __node_2_se(node) rb_entry((node), struct sched_entity, run_node)

static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	if (curr && curr->on_rq) {
		s64 delta = (s64)(curr->vruntime - cfs_rq->min_vruntime);
		if (delta > 0)
			cfs_rq->min_vruntime = curr->vruntime;
	}
}

static inline bool __entity_less(struct rb_node *a, const struct rb_node *b)
{
	return (s64)(__node_2_se(a)->vruntime - __node_2_se(b)->vruntime) < 0;
}

static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	rb_add_cached(&se->run_node, &cfs_rq->tasks_timeline, __entity_less);
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	rb_erase_cached(&se->run_node, &cfs_rq->tasks_timeline);
}

static struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *left = rb_first_cached(&cfs_rq->tasks_timeline);

	if (!left)
		return NULL;

	return __node_2_se(left);
}

static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_clock_task(rq_of(cfs_rq));
	u64 delta_exec;

	if (unlikely(!curr))
		return;

	delta_exec = now - curr->exec_start;
	if (unlikely((s64)delta_exec <= 0))
		return;

	curr->exec_start = now;

	curr->sum_exec_runtime += delta_exec;
	curr->vruntime += delta_exec;
	update_min_vruntime(cfs_rq);
}

void reweight_task(struct task_struct *p, int prio)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	unsigned long weight = scale_load(sched_prio_to_weight[prio]);

	if (se->on_rq) {
		if (cfs_rq->curr == se)
			update_curr(cfs_rq);
		update_load_sub(&cfs_rq->load, se->load.weight);
	}
	se->load.weight = weight;
	if (se->on_rq)
		update_load_add(&cfs_rq->load, se->load.weight);
}

static void place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			 int initial)
{
	se->vruntime = cfs_rq->min_vruntime;
}

static void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (se->on_rq)
		__dequeue_entity(cfs_rq, se);

	se->exec_start = rq_clock_task(rq_of(cfs_rq));
	cfs_rq->curr = se;
}

static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	if (se->on_rq)
		goto out;
	cfs_rq = cfs_rq_of(se);
	update_curr(cfs_rq);
	if (!(flags & ENQUEUE_WAKEUP))
		se->vruntime += cfs_rq->min_vruntime;
	update_load_add(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running++;
	if (flags & ENQUEUE_WAKEUP)
		place_entity(cfs_rq, se, 0);
	if (cfs_rq->curr != se)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;
	cfs_rq->h_nr_running++;
out:
	rq->nr_running++;
}

static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	cfs_rq = cfs_rq_of(se);
	update_curr(cfs_rq);
	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	se->on_rq = 0;
	update_load_sub(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running--;
	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;
	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) != DEQUEUE_SAVE)
		update_min_vruntime(cfs_rq);
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
	struct sched_entity *se;

	if (!sched_fair_runnable(rq))
		return NULL;

	if (prev)
		put_prev_task(rq, prev);

	se = __pick_first_entity(cfs_rq);
	set_next_entity(cfs_rq, se);
	return task_of(se);
}

static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	if (se->on_rq) {
		update_curr(cfs_rq);
		__enqueue_entity(cfs_rq, se);
	}
	cfs_rq->curr = NULL;
}

static void task_fork_fair(struct task_struct *p)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se, *curr;
	struct rq *rq = this_rq();
	struct rq_flags rf;

	rq_lock(rq, &rf);
	update_rq_clock(rq);

	cfs_rq = task_cfs_rq(current);
	curr = cfs_rq->curr;
	if (curr) {
		update_curr(cfs_rq);
		se->vruntime = curr->vruntime;
	}
	place_entity(cfs_rq, se, 1);
	se->vruntime -= cfs_rq->min_vruntime;
	rq_unlock(rq, &rf);
}

void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->tasks_timeline = RB_ROOT_CACHED;
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
}

DEFINE_SCHED_CLASS(fair) = {

	.enqueue_task = enqueue_task_fair,
	.dequeue_task = dequeue_task_fair,

	.check_preempt_curr = check_preempt_wakeup,

	.put_prev_task = put_prev_task_fair,

	.task_fork = task_fork_fair,

};
