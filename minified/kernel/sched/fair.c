
#include <linux/jiffies.h>

#include <linux/topology.h>
#include <linux/sched/clock.h>

#include <linux/sched/cputime.h>
#include <linux/sched/isolation.h>

#include <linux/interrupt.h>

#include <asm/switch_to.h>

#include "sched.h"
#include "stats.h"

unsigned int sysctl_sched_latency = 6000000ULL;
unsigned int sysctl_sched_min_granularity = 750000ULL;
static unsigned int sched_nr_latency = 8;
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

#define WMULT_CONST (~0U)
#define WMULT_SHIFT 32
/* __update_inv_weight inlined into __calc_delta */

static u64 __calc_delta(u64 delta_exec, unsigned long weight,
			struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	u32 fact_hi = (u32)(fact >> 32);
	int shift = WMULT_SHIFT;
	int fs;

	/* __update_inv_weight inlined */
	if (!lw->inv_weight) {
		unsigned long w = scale_load_down(lw->weight);

		if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
			lw->inv_weight = 1;
		else if (unlikely(!w))
			lw->inv_weight = WMULT_CONST;
		else
			lw->inv_weight = WMULT_CONST / w;
	}

	if (unlikely(fact_hi)) {
		fs = fls(fact_hi);
		shift -= fs;
		fact >>= fs;
	}

	fact = mul_u32_u32(fact, lw->inv_weight);

	fact_hi = (u32)(fact >> 32);
	if (fact_hi) {
		fs = fls(fact_hi);
		shift -= fs;
		fact >>= fs;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}

const struct sched_class fair_sched_class;

#define for_each_sched_entity(se) for (; se; se = NULL)

#define for_each_leaf_cfs_rq_safe(rq, cfs_rq, pos) \
	for (cfs_rq = &rq->cfs, pos = NULL; cfs_rq; cfs_rq = pos)

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

/* min_vruntime, entity_before inlined into callers */

#define __node_2_se(node) rb_entry((node), struct sched_entity, run_node)

static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	struct rb_node *leftmost = rb_first_cached(&cfs_rq->tasks_timeline);

	u64 vruntime = cfs_rq->min_vruntime;

	if (curr) {
		if (curr->on_rq)
			vruntime = curr->vruntime;
		else
			curr = NULL;
	}

	if (leftmost) {
		struct sched_entity *se = __node_2_se(leftmost);

		if (!curr)
			vruntime = se->vruntime;
		else {
			/* min_vruntime inlined */
			s64 delta = (s64)(se->vruntime - vruntime);
			if (delta < 0)
				vruntime = se->vruntime;
		}
	}

	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
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

static inline u64 calc_delta_fair(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load);

	return delta;
}

/* __sched_period inlined */

static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned int nr_running = cfs_rq->nr_running;
	unsigned int min_gran;
	u64 slice;

	if (sched_feat(ALT_PERIOD))
		nr_running = rq_of(cfs_rq)->cfs.h_nr_running;

	nr_running += !se->on_rq;
	if (unlikely(nr_running > sched_nr_latency))
		slice = nr_running * sysctl_sched_min_granularity;
	else
		slice = sysctl_sched_latency;

	for_each_sched_entity(se)
	{
		struct load_weight *load;
		struct load_weight lw;
		struct cfs_rq *qcfs_rq;

		qcfs_rq = cfs_rq_of(se);
		load = &qcfs_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = qcfs_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}

	if (sched_feat(BASE_SLICE)) {
		/* se_is_idle always 0 - use min_granularity directly */
		min_gran = sysctl_sched_min_granularity;
		slice = max_t(u64, slice, min_gran);
	}

	return slice;
}

/* sched_vslice inlined into place_entity */

#include "pelt.h"

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
	curr->vruntime += calc_delta_fair(delta_exec, curr);
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

#define UPDATE_TG 0x0
#define SKIP_AGE_LOAD 0x0
#define DO_ATTACH 0x0

/* update_load_avg removed - schedstat always disabled, empty body */
#define update_load_avg(cfs_rq, se, flags) \
	do {                               \
	} while (0)

static void place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			 int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;

	if (initial && sched_feat(START_DEBIT))
		/* sched_vslice inlined */
		vruntime += calc_delta_fair(sched_slice(cfs_rq, se), se);

	if (!initial) {
		unsigned long thresh;
		/* se_is_idle() always 0 - use sysctl_sched_latency */
		thresh = sysctl_sched_latency;

		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		vruntime -= thresh;
	}

	se->vruntime = max_vruntime(se->vruntime, vruntime);
}

static void enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			   int flags)
{
	bool renorm = !(flags & ENQUEUE_WAKEUP); /* ENQUEUE_MIGRATED=0 */
	bool curr = cfs_rq->curr == se;

	if (renorm && curr)
		se->vruntime += cfs_rq->min_vruntime;

	update_curr(cfs_rq);

	if (renorm && !curr)
		se->vruntime += cfs_rq->min_vruntime;

	update_load_avg(cfs_rq, se, UPDATE_TG | DO_ATTACH);
	update_load_add(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running++;

	if (flags & ENQUEUE_WAKEUP)
		place_entity(cfs_rq, se, 0);

	if (!curr)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;
}

/* clear_buddies removed - buddy tracking only matters with multiple tasks (~32 LOC) */
/* stub function also removed - calls replaced with nothing */

static void dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			   int flags)
{
	update_curr(cfs_rq);

	update_load_avg(cfs_rq, se, UPDATE_TG);

	/* clear_buddies call removed - empty stub */

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	se->on_rq = 0;
	update_load_sub(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running--;

	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;

	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) != DEQUEUE_SAVE)
		update_min_vruntime(cfs_rq);
}

/* check_preempt_tick removed - only called when nr_running > 1, which never happens
   in single-task kernel (~27 LOC) */

static void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* clear_buddies call removed - empty stub */

	if (se->on_rq) {
		__dequeue_entity(cfs_rq, se);
		update_load_avg(cfs_rq, se, UPDATE_TG);
	}

	/* Inlined update_stats_curr_start */
	se->exec_start = rq_clock_task(rq_of(cfs_rq));
	cfs_rq->curr = se;
	/* schedstat_enabled() always 0, block removed */

	se->prev_sum_exec_runtime = se->sum_exec_runtime;
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
		enqueue_entity(cfs_rq, se, flags);

		cfs_rq->h_nr_running++;
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */
		flags = ENQUEUE_WAKEUP;
	}
	/* Second for_each_sched_entity loop removed - se is already NULL after first loop */

	add_nr_running(rq, 1);
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
		dequeue_entity(cfs_rq, se, flags);

		cfs_rq->h_nr_running--;
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */

		if (cfs_rq->load.weight) {
			/* parent_entity always NULL - break is always taken */
			break;
		}
		flags |= DEQUEUE_SLEEP;
	}
	/* Second for_each_sched_entity loop removed - se is already NULL after first loop */

	sub_nr_running(rq, 1);
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
		s64 gran, vdiff = se->vruntime - pse->vruntime;
		if (vdiff > 0) {
			gran = calc_delta_fair(sysctl_sched_wakeup_granularity,
					       pse);
			if (vdiff > gran)
				goto preempt;
		}
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
	update_idle_rq_clock_pelt(rq);
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
		update_load_avg(cfs_rq, se, UPDATE_TG);
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

static void prio_changed_fair(struct rq *rq, struct task_struct *p, int oldprio)
{
	if (!task_on_rq_queued(p))
		return;

	if (rq->cfs.nr_running == 1)
		return;

	if (task_current(rq, p)) {
		if (p->prio > oldprio)
			resched_curr(rq);
	} else
		check_preempt_curr(rq, p, 0);
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

static void detach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	if (!vruntime_normalized(p)) {
		place_entity(cfs_rq, se, 0);
		se->vruntime -= cfs_rq->min_vruntime;
	}

	update_load_avg(cfs_rq, se, 0);
}

static void attach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	update_load_avg(cfs_rq, se,
			sched_feat(ATTACH_AGE_LOAD) ? 0 : SKIP_AGE_LOAD);

	if (!vruntime_normalized(p))
		se->vruntime += cfs_rq->min_vruntime;
}

static void switched_from_fair(struct rq *rq, struct task_struct *p)
{
	detach_task_cfs_rq(p);
}

static void switched_to_fair(struct rq *rq, struct task_struct *p)
{
	attach_task_cfs_rq(p);

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
