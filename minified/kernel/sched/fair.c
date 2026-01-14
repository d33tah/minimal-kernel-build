
/* mmap_lock.h, highmem.h, mempolicy.h, ratelimit.h, task_work.h removed - unused */
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
/* normalized_sysctl_sched_latency, sysctl_sched_tunable_scaling removed - unused */
unsigned int sysctl_sched_min_granularity = 750000ULL;
/* normalized_sysctl_sched_min_granularity, sysctl_sched_idle_min_granularity, sysctl_sched_child_runs_first removed - unused */
static unsigned int sched_nr_latency = 8;
unsigned int sysctl_sched_wakeup_granularity = 1000000UL;
/* normalized_sysctl_sched_wakeup_granularity removed - unused */

/* sched_thermal_decay_shift removed - unused */

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

static void __update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;

	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = WMULT_CONST;
	else
		lw->inv_weight = WMULT_CONST / w;
}

static u64 __calc_delta(u64 delta_exec, unsigned long weight,
			struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	u32 fact_hi = (u32)(fact >> 32);
	int shift = WMULT_SHIFT;
	int fs;

	__update_inv_weight(lw);

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

/* list_add_leaf_cfs_rq, assert_list_leaf_cfs_rq, find_matching_se removed - empty stubs */
#define for_each_leaf_cfs_rq_safe(rq, cfs_rq, pos) \
	for (cfs_rq = &rq->cfs, pos = NULL; cfs_rq; cfs_rq = pos)

/* parent_entity, cfs_rq_is_idle, se_is_idle removed - always return constants */

/* account_cfs_rq_runtime forward decl removed - empty stub */

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline bool entity_before(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

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
		else
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
}

static inline bool __entity_less(struct rb_node *a, const struct rb_node *b)
{
	return entity_before(__node_2_se(a), __node_2_se(b));
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

static u64 __sched_period(unsigned long nr_running)
{
	if (unlikely(nr_running > sched_nr_latency))
		return nr_running * sysctl_sched_min_granularity;
	else
		return sysctl_sched_latency;
}

/* sched_idle_cfs_rq removed - always returns false (idle_nr_running never set) */

static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned int nr_running = cfs_rq->nr_running;
	/* init_se removed - was unused */
	unsigned int min_gran;
	u64 slice;

	if (sched_feat(ALT_PERIOD))
		nr_running = rq_of(cfs_rq)->cfs.h_nr_running;

	slice = __sched_period(nr_running + !se->on_rq);

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
/* init_entity_runnable_average, post_init_entity_util_avg, update_tg_load_avg removed - never called */

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

	/* account_group_exec_runtime, account_cfs_rq_runtime removed - stubs */
}

static void update_curr_fair(struct rq *rq)
{
	update_curr(cfs_rq_of(&rq->curr->se));
}

/* update_stats functions removed - schedstat_enabled() is always 0 */
#define update_stats_wait_start_fair(cfs_rq, se) \
	do {                                     \
	} while (0)
#define update_stats_wait_end_fair(cfs_rq, se) \
	do {                                   \
	} while (0)
#define update_stats_enqueue_sleeper_fair(cfs_rq, se) \
	do {                                          \
	} while (0)
#define update_stats_enqueue_fair(cfs_rq, se, flags) \
	do {                                         \
	} while (0)
#define update_stats_dequeue_fair(cfs_rq, se, flags) \
	do {                                         \
	} while (0)

/* update_stats_curr_start inlined into set_next_entity */

static void account_entity_enqueue(struct cfs_rq *cfs_rq,
				   struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running++;
	/* se_is_idle() always 0, idle_nr_running++ removed */
}

static void account_entity_dequeue(struct cfs_rq *cfs_rq,
				   struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running--;
	/* se_is_idle() always 0, idle_nr_running-- removed */
}

/* add_positive, sub_positive, lsub_positive removed - unused macros */

/* enqueue_load_avg, dequeue_load_avg removed - empty stubs */

static void reweight_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			    unsigned long weight)
{
	if (se->on_rq) {
		if (cfs_rq->curr == se)
			update_curr(cfs_rq);
		update_load_sub(&cfs_rq->load, se->load.weight);
	}
	/* dequeue_load_avg, update_load_set inlined, enqueue_load_avg removed */
	se->load.weight = weight;
	se->load.inv_weight = 0;
	if (se->on_rq)
		update_load_add(&cfs_rq->load, se->load.weight);
}

void reweight_task(struct task_struct *p, int prio)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	struct load_weight *load = &se->load;
	unsigned long weight = scale_load(sched_prio_to_weight[prio]);

	reweight_entity(cfs_rq, se, weight);
	load->inv_weight = sched_prio_to_wmult[prio];
}

/* update_cfs_group, cfs_rq_util_change, cpufreq_update_util removed - empty stubs */

#define UPDATE_TG 0x0
#define SKIP_AGE_LOAD 0x0
#define DO_ATTACH 0x0

static inline void update_load_avg(struct cfs_rq *cfs_rq,
				   struct sched_entity *se, int not_used1)
{
	/* cfs_rq_util_change removed - was empty stub chain */
}

/* attach_entity_load_avg, detach_entity_load_avg removed - stubs */
/* util_est_enqueue, util_est_dequeue, util_est_update, newidle_balance removed - empty stubs */

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

/* check_enqueue_throttle forward decl removed - stub removed */

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
	/* se_update_runnable removed - empty stub */
	/* update_cfs_group removed - empty stub */
	account_entity_enqueue(cfs_rq, se);

	if (flags & ENQUEUE_WAKEUP)
		place_entity(cfs_rq, se, 0);

	check_schedstat_required();
	update_stats_enqueue_fair(cfs_rq, se, flags);
	if (!curr)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;

	/* list_add_leaf_cfs_rq, check_enqueue_throttle removed - empty stubs */
}

/* __clear_buddies_last/next/skip inlined into clear_buddies */
static void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct sched_entity *tmp;

	if (cfs_rq->last == se) {
		for_each_sched_entity(tmp = se)
		{
			struct cfs_rq *c = cfs_rq_of(tmp);
			if (c->last != tmp)
				break;
			c->last = NULL;
		}
	}
	if (cfs_rq->next == se) {
		for_each_sched_entity(tmp = se)
		{
			struct cfs_rq *c = cfs_rq_of(tmp);
			if (c->next != tmp)
				break;
			c->next = NULL;
		}
	}
	if (cfs_rq->skip == se) {
		for_each_sched_entity(tmp = se)
		{
			struct cfs_rq *c = cfs_rq_of(tmp);
			if (c->skip != tmp)
				break;
			c->skip = NULL;
		}
	}
}

/* return_cfs_rq_runtime forward decl removed - stub removed */

static void dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			   int flags)
{
	update_curr(cfs_rq);

	update_load_avg(cfs_rq, se, UPDATE_TG);
	/* se_update_runnable removed - empty stub */

	update_stats_dequeue_fair(cfs_rq, se, flags);

	clear_buddies(cfs_rq, se);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	se->on_rq = 0;
	account_entity_dequeue(cfs_rq, se);

	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;

	/* return_cfs_rq_runtime() removed - empty stub */
	/* update_cfs_group removed - empty stub */
	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) != DEQUEUE_SAVE)
		update_min_vruntime(cfs_rq);
}

static void check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;
	struct sched_entity *se;
	s64 delta;

	ideal_runtime = sched_slice(cfs_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	if (delta_exec > ideal_runtime) {
		resched_curr(rq_of(cfs_rq));

		clear_buddies(cfs_rq, curr);
		return;
	}

	if (delta_exec < sysctl_sched_min_granularity)
		return;

	se = __pick_first_entity(cfs_rq);
	delta = curr->vruntime - se->vruntime;

	if (delta < 0)
		return;

	if (delta > ideal_runtime)
		resched_curr(rq_of(cfs_rq));
}

static void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	clear_buddies(cfs_rq, se);

	if (se->on_rq) {
		update_stats_wait_end_fair(cfs_rq, se);
		__dequeue_entity(cfs_rq, se);
		update_load_avg(cfs_rq, se, UPDATE_TG);
	}

	/* Inlined update_stats_curr_start */
	se->exec_start = rq_clock_task(rq_of(cfs_rq));
	cfs_rq->curr = se;
	/* schedstat_enabled() always 0, block removed */

	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int wakeup_preempt_entity(struct sched_entity *curr,
				 struct sched_entity *se);

static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq,
					     struct sched_entity *curr)
{
	struct sched_entity *left = __pick_first_entity(cfs_rq);
	struct sched_entity *se;

	if (!left || (curr && entity_before(curr, left)))
		left = curr;

	se = left;

	if (cfs_rq->skip && cfs_rq->skip == se) {
		struct sched_entity *second;
		struct rb_node *next;

		if (se == curr) {
			second = __pick_first_entity(cfs_rq);
		} else {
			/* Inlined __pick_next_entity */
			next = rb_next(&se->run_node);
			second = next ? __node_2_se(next) : NULL;
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1) {
		se = cfs_rq->next;
	} else if (cfs_rq->last &&
		   wakeup_preempt_entity(cfs_rq->last, left) < 1) {
		se = cfs_rq->last;
	}

	return se;
}

/* check_cfs_rq_runtime forward decl removed - stub always returns false */

static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	if (prev->on_rq)
		update_curr(cfs_rq);
	/* check_cfs_rq_runtime() call removed - always returns false */
	if (prev->on_rq) {
		update_stats_wait_start_fair(cfs_rq, prev);

		__enqueue_entity(cfs_rq, prev);

		update_load_avg(cfs_rq, prev, 0);
	}
	cfs_rq->curr = NULL;
}

static void entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr,
			int queued)
{
	update_curr(cfs_rq);

	update_load_avg(cfs_rq, curr, UPDATE_TG);
	/* update_cfs_group removed - empty stub */
	if (cfs_rq->nr_running > 1)
		check_preempt_tick(cfs_rq, curr);
}

/* account_cfs_rq_runtime, check_cfs_rq_runtime, check_enqueue_throttle, return_cfs_rq_runtime removed - stubs */
/* cfs_rq_throttled, sched_idle_rq, throttled_hierarchy, hrtick_start_fair, sched_idle_cfs_rq removed - unused */

static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	/* util_est_enqueue, cpufreq_update_util removed - empty stubs */
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

	for_each_sched_entity(se)
	{
		cfs_rq = cfs_rq_of(se);

		update_load_avg(cfs_rq, se, UPDATE_TG);
		/* se_update_runnable, update_cfs_group removed - empty stubs */
		cfs_rq->h_nr_running++;
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */
	}

	add_nr_running(rq, 1);
	/* enqueue_throttle label and assert_list_leaf_cfs_rq removed */
}

static void set_next_buddy(struct sched_entity *se);

static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
	/* task_sleep, was_sched_idle, idle_h_nr_running removed - write-only */

	/* util_est_dequeue removed - empty stub */
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

	for_each_sched_entity(se)
	{
		cfs_rq = cfs_rq_of(se);

		update_load_avg(cfs_rq, se, UPDATE_TG);
		/* se_update_runnable, update_cfs_group removed - empty stubs */
		cfs_rq->h_nr_running--;
		/* idle_h_nr_running, cfs_rq_is_idle, cfs_rq_throttled always 0 - removed */
	}

	sub_nr_running(rq, 1);
	/* dequeue_throttle label and util_est_update removed */
}

static int wakeup_preempt_entity(struct sched_entity *curr,
				 struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

	/* Inlined wakeup_gran */
	gran = calc_delta_fair(sysctl_sched_wakeup_granularity, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

static void set_next_buddy(struct sched_entity *se)
{
	for_each_sched_entity(se)
	{
		if (SCHED_WARN_ON(!se->on_rq))
			return;
		/* se_is_idle() always 0 - skip */
		cfs_rq_of(se)->next = se;
	}
}

static void check_preempt_wakeup(struct rq *rq, struct task_struct *p,
				 int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->se, *pse = &p->se;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	int scale = cfs_rq->nr_running >= sched_nr_latency;
	int next_buddy_marked = 0;
	/* cse_is_idle, pse_is_idle removed - se_is_idle always 0 */

	if (unlikely(se == pse))
		return;
	/* throttled_hierarchy() always 0 - skip */
	if (sched_feat(NEXT_BUDDY) && scale && !(wake_flags & WF_FORK)) {
		set_next_buddy(pse);
		next_buddy_marked = 1;
	}

	if (test_tsk_need_resched(curr))
		return;

	if (unlikely(task_has_idle_policy(curr)) &&
	    likely(!task_has_idle_policy(p)))
		goto preempt;

	if (unlikely(p->policy != SCHED_NORMAL) ||
	    !sched_feat(WAKEUP_PREEMPTION))
		return;

	/* find_matching_se removed - empty stub */
	BUG_ON(!pse);
	/* se_is_idle always 0 - both cse_is_idle/pse_is_idle are 0, conditions skipped */
	update_curr(cfs_rq_of(se));
	if (wakeup_preempt_entity(se, pse) == 1) {
		if (!next_buddy_marked)
			set_next_buddy(pse);
		goto preempt;
	}

	return;

preempt:
	resched_curr(rq);

	if (unlikely(!se->on_rq || curr == rq->idle))
		return;

	/* Inlined set_last_buddy */
	if (sched_feat(LAST_BUDDY) && scale && entity_is_task(se)) {
		for_each_sched_entity(se)
		{
			if (SCHED_WARN_ON(!se->on_rq))
				break;
			cfs_rq_of(se)->last = se;
		}
	}
}

struct task_struct *pick_next_task_fair(struct rq *rq, struct task_struct *prev,
					struct rq_flags *rf)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;
	struct task_struct *p;
	/* new_tasks removed - newidle_balance always returns 0 */

	/* again: label removed - unused */
	if (!sched_fair_runnable(rq))
		goto idle;

	if (prev)
		put_prev_task(rq, prev);

	/* group_cfs_rq always returns NULL, loop simplified */
	se = pick_next_entity(cfs_rq, NULL);
	set_next_entity(cfs_rq, se);

	p = task_of(se);

	/* done: label removed - unused */
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
		put_prev_entity(cfs_rq, se);
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
		entity_tick(cfs_rq, se, queued);
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

static void detach_entity_cfs_rq(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	update_load_avg(cfs_rq, se, 0);
}

static void attach_entity_cfs_rq(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	update_load_avg(cfs_rq, se,
			sched_feat(ATTACH_AGE_LOAD) ? 0 : SKIP_AGE_LOAD);
}

static void detach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	if (!vruntime_normalized(p)) {
		place_entity(cfs_rq, se, 0);
		se->vruntime -= cfs_rq->min_vruntime;
	}

	detach_entity_cfs_rq(se);
}

static void attach_task_cfs_rq(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	attach_entity_cfs_rq(se);

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
		/* account_cfs_rq_runtime removed - empty stub */
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
	/* .yield_task removed - callback never called */
	/* .yield_to_task removed - callback never called */

	.check_preempt_curr = check_preempt_wakeup,

	.pick_next_task = __pick_next_task_fair,
	.put_prev_task = put_prev_task_fair,
	.set_next_task = set_next_task_fair,

	.task_tick = task_tick_fair,
	.task_fork = task_fork_fair,

	.prio_changed = prio_changed_fair,
	.switched_from = switched_from_fair,
	.switched_to = switched_to_fair,

	/* .get_rr_interval removed - callback never called */

	.update_curr = update_curr_fair,

};
/* init_sched_fair_class removed - empty stub */
