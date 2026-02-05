
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
/* topology.h, sched/signal.h, sched/isolation.h, interrupt.h, switch_to.h removed - unused */

#include "sched.h"
/* stats.h removed - was empty header */

/* sysctl_sched_latency, sysctl_sched_min_granularity removed - never used */
unsigned int sysctl_sched_wakeup_granularity = 1000000UL;

static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
	lw->inv_weight = 0;
}

/* sched_init_granularity, get_update_sysctl_factor, update_sysctl, normalized_sysctl_* removed -
   sysctl values already set to correct values at compile time */

/* WMULT_CONST/WMULT_SHIFT removed - unused after __calc_delta simplification */
/* __calc_delta inlined - just returns delta_exec for single-process kernel (~35 LOC saved originally) */

const struct sched_class fair_sched_class;

#define for_each_sched_entity(se) for (; se; se = NULL)

#define for_each_leaf_cfs_rq_safe(rq, cfs_rq, pos) \
	for (cfs_rq = &rq->cfs, pos = NULL; cfs_rq; cfs_rq = pos)

/* max_vruntime, min_vruntime, entity_before inlined into callers */

#define __node_2_se(node) rb_entry((node), struct sched_entity, run_node)

/* Simplified - single-process kernel has only one entity (~22 LOC) */
static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	/* max_vruntime inlined */
	if (curr && curr->on_rq) {
		s64 delta = (s64)(curr->vruntime - cfs_rq->min_vruntime);
		if (delta > 0)
			cfs_rq->min_vruntime = curr->vruntime;
	}
}

static inline bool __entity_less(struct rb_node *a, const struct rb_node *b)
{
	/* Inlined entity_before */
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

struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *left = rb_first_cached(&cfs_rq->tasks_timeline);

	if (!left)
		return NULL;

	return __node_2_se(left);
}

/* calc_delta_fair removed - just returned delta, inlined at call sites */
/* sched_slice inlined into place_entity */
/* pelt.h removed - was empty stubs */

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
	/* schedstat_enabled() always 0, block removed */

	curr->sum_exec_runtime += delta_exec;
	/* calc_delta_fair inlined - just returns delta for single-process kernel */
	curr->vruntime += delta_exec;
	update_min_vruntime(cfs_rq);
}

static void update_curr_fair(struct rq *rq)
{
	update_curr(cfs_rq_of(&rq->curr->se));
}

/* update_stats_* macros removed - schedstat_enabled() always 0, no calls needed */

/* update_stats_curr_start, account_entity_enqueue/dequeue, reweight_entity inlined */

void reweight_task(struct task_struct *p, int prio)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	struct load_weight *load = &se->load;
	unsigned long weight = scale_load(sched_prio_to_weight[prio]);

	/* reweight_entity inlined */
	if (se->on_rq) {
		if (cfs_rq->curr == se)
			update_curr(cfs_rq);
		update_load_sub(&cfs_rq->load, se->load.weight);
	}
	se->load.weight = weight;
	se->load.inv_weight = 0;
	if (se->on_rq)
		update_load_add(&cfs_rq->load, se->load.weight);
	load->inv_weight = sched_prio_to_wmult[prio];
}

/* 0, 0, 0, update_load_avg removed - schedstat always disabled */
#define update_load_avg(cfs_rq, se, flags) \
	do {                               \
	} while (0)

/* Stub: single-task kernel, vruntime doesn't matter for scheduling */
static void place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			 int initial)
{
	se->vruntime = cfs_rq->min_vruntime;
}

/* enqueue_entity, dequeue_entity, clear_buddies inlined/removed */

/* check_preempt_tick removed - only called when nr_running > 1, which never happens
   in single-task kernel (~27 LOC) */

static void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* clear_buddies call removed - empty stub */

	if (se->on_rq) {
		__dequeue_entity(cfs_rq, se);
		update_load_avg(cfs_rq, se, 0);
	}

	/* Inlined update_stats_curr_start */
	se->exec_start = rq_clock_task(rq_of(cfs_rq));
	cfs_rq->curr = se;
	/* schedstat_enabled() always 0, block removed */
	/* se->prev_sum_exec_runtime assignment removed - write-only field removed */
}

/* pick_next_entity inlined - buddy selection code removed for single-task kernel */
/* put_prev_entity inlined into put_prev_task_fair */

/* entity_tick inlined */

static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se)
	{
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		/* enqueue_entity inlined */
		{
			bool renorm = !(flags & ENQUEUE_WAKEUP);
			bool curr = cfs_rq->curr == se;
			if (renorm && curr)
				se->vruntime += cfs_rq->min_vruntime;
			update_curr(cfs_rq);
			if (renorm && !curr)
				se->vruntime += cfs_rq->min_vruntime;
			update_load_avg(cfs_rq, se, 0);
			update_load_add(&cfs_rq->load, se->load.weight);
			cfs_rq->nr_running++;
			if (flags & ENQUEUE_WAKEUP)
				place_entity(cfs_rq, se, 0);
			if (!curr)
				__enqueue_entity(cfs_rq, se);
			se->on_rq = 1;
		}

		cfs_rq->h_nr_running++;
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */
		flags = ENQUEUE_WAKEUP;
	}
	/* Second for_each_sched_entity loop removed - se is already NULL after first loop */

	rq->nr_running++; /* add_nr_running inlined - single caller */
	/* enqueue_throttle label and assert_list_leaf_cfs_rq removed */
}

/* set_next_buddy forward decl removed - function never called */

static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
	/* task_sleep, was_sched_idle, idle_h_nr_running removed - write-only */

	for_each_sched_entity(se)
	{
		cfs_rq = cfs_rq_of(se);
		/* dequeue_entity inlined */
		update_curr(cfs_rq);
		update_load_avg(cfs_rq, se, 0);
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
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */

		if (cfs_rq->load.weight) {
			/* parent_entity always NULL - break is always taken */
			break;
		}
		flags |= DEQUEUE_SLEEP;
	}
	/* Second for_each_sched_entity loop removed - se is already NULL after first loop */

	rq->nr_running--; /* sub_nr_running inlined - single caller */
	/* dequeue_throttle label and util_est_update removed */
}

/* wakeup_preempt_entity inlined - single caller */
/* set_next_buddy removed - buddy tracking dead with single task (~10 LOC) */
/* stub function also removed - never called */

/* check_preempt_wakeup simplified - removed buddy code, dead scale variable (~17 LOC) */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p,
				 int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->se, *pse = &p->se;

	if (unlikely(se == pse))
		return;

	if (test_tsk_need_resched(curr))
		return;

	if (unlikely(task_has_idle_policy(curr)) &&
	    likely(!task_has_idle_policy(p)))
		goto preempt;

	if (unlikely(p->policy != SCHED_NORMAL) ||
	    !sched_feat(WAKEUP_PREEMPTION))
		return;

	update_curr(cfs_rq_of(se));
	/* wakeup_preempt_entity inlined */
	{
		/* calc_delta_fair inlined - just returns the constant */
		s64 vdiff = se->vruntime - pse->vruntime;
		if (vdiff > (s64)sysctl_sched_wakeup_granularity)
			goto preempt;
	}

	return;

preempt:
	resched_curr(rq);
}

struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev,
					struct rq_flags *rf)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;
	struct task_struct *p;
	/* new_tasks removed - newidle_balance always returns 0 */

	if (!sched_fair_runnable(rq))
		goto idle;

	if (prev)
		put_prev_task(rq, prev);

	/* group_cfs_rq always returns NULL, loop simplified */
	se = __pick_first_entity(cfs_rq);
	set_next_entity(cfs_rq, se);

	p = task_of(se);

	/* hrtick_enabled_fair always returns 0, dead if block removed */
	return p;

idle:
	if (!rf)
		return NULL;
	/* newidle_balance always returns 0, dead branches removed */
	/* update_idle_rq_clock_pelt removed - was empty stub */
	return NULL;
}

static struct task_struct *__pick_next_task_fair(struct rq *rq)
{
	return pick_next_task_fair(rq, NULL, NULL);
}

static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

	for_each_sched_entity(se)
	{
		cfs_rq = cfs_rq_of(se);
		/* put_prev_entity inlined */
		if (se->on_rq)
			update_curr(cfs_rq);
		if (se->on_rq) {
			__enqueue_entity(cfs_rq, se);
			update_load_avg(cfs_rq, se, 0);
		}
		cfs_rq->curr = NULL;
	}
}

/* yield_task_fair removed - callback never called (~20 LOC) */

/* yield_to_task_fair removed - callback never called (~12 LOC) */

static void task_tick_fair(struct rq *rq, struct task_struct *curr, int queued)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se)
	{
		cfs_rq = cfs_rq_of(se);
		update_curr(cfs_rq);
		update_load_avg(cfs_rq, se, 0);
	}
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
	/* sysctl_sched_child_runs_first check removed - always 0 (~4 LOC) */
	se->vruntime -= cfs_rq->min_vruntime;
	rq_unlock(rq, &rf);
}

/* Stubbed - single-task kernel doesn't need priority preemption (~13 LOC) */
static void prio_changed_fair(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static inline bool vruntime_normalized(struct task_struct *p)
{
	struct sched_entity *se = &p->se;

	if (p->on_rq)
		return true;

	if (!se->sum_exec_runtime ||
	    (READ_ONCE(p->__state) == TASK_WAKING && p->sched_remote_wakeup))
		return true;

	return false;
}

/* detach_entity_cfs_rq, attach_entity_cfs_rq inlined */
/* detach_task_cfs_rq, attach_task_cfs_rq inlined */

static void switched_from_fair(struct rq *rq, struct task_struct *p)
{
	/* detach_task_cfs_rq inlined */
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	if (!vruntime_normalized(p)) {
		place_entity(cfs_rq, se, 0);
		se->vruntime -= cfs_rq->min_vruntime;
	}

	update_load_avg(cfs_rq, se, 0);
}

static void switched_to_fair(struct rq *rq, struct task_struct *p)
{
	/* attach_task_cfs_rq inlined */
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	update_load_avg(cfs_rq, se, 0);

	if (!vruntime_normalized(p))
		se->vruntime += cfs_rq->min_vruntime;

	if (task_on_rq_queued(p)) {
		if (task_current(rq, p))
			resched_curr(rq);
		else
			check_preempt_curr(rq, p, 0);
	}
}

static void set_next_task_fair(struct rq *rq, struct task_struct *p, bool first)
{
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se)
	{
		struct cfs_rq *cfs_rq = cfs_rq_of(se);

		set_next_entity(cfs_rq, se);
	}
}

void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->tasks_timeline = RB_ROOT_CACHED;
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
}

/* get_rr_interval_fair removed - callback never called (~10 LOC) */

DEFINE_SCHED_CLASS(fair) = {

	.enqueue_task = enqueue_task_fair,
	.dequeue_task = dequeue_task_fair,

	.check_preempt_curr = check_preempt_wakeup,

	.pick_next_task = __pick_next_task_fair,
	.put_prev_task = put_prev_task_fair,
	.set_next_task = set_next_task_fair,

	.task_tick = task_tick_fair,
	.task_fork = task_fork_fair,

	.prio_changed = prio_changed_fair,
	.switched_from = switched_from_fair,
	.switched_to = switched_to_fair,

	.update_curr = update_curr_fair,

};
