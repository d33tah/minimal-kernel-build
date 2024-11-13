/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_STATS_H
#define _KERNEL_STATS_H

#ifdef CONFIG_SCHEDSTATS

extern struct static_key_false sched_schedstats;

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcount++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_cpu_time += delta;
}

static inline void
rq_sched_info_dequeue(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.run_delay += delta;
}
#define   schedstat_enabled()		static_branch_unlikely(&sched_schedstats)
#define __schedstat_inc(var)		do { var++; } while (0)
#define   schedstat_inc(var)		do { if (schedstat_enabled()) { var++; } } while (0)
#define __schedstat_add(var, amt)	do { var += (amt); } while (0)
#define   schedstat_add(var, amt)	do { if (schedstat_enabled()) { var += (amt); } } while (0)
#define __schedstat_set(var, val)	do { var = (val); } while (0)
#define   schedstat_set(var, val)	do { if (schedstat_enabled()) { var = (val); } } while (0)
#define   schedstat_val(var)		(var)
#define   schedstat_val_or_zero(var)	((schedstat_enabled()) ? (var) : 0)

void __update_stats_wait_start(struct rq *rq, struct task_struct *p,
			       struct sched_statistics *stats);

void __update_stats_wait_end(struct rq *rq, struct task_struct *p,
			     struct sched_statistics *stats);
void __update_stats_enqueue_sleeper(struct rq *rq, struct task_struct *p,
				    struct sched_statistics *stats);

static inline void
check_schedstat_required(void)
{
	if (schedstat_enabled())
		return;

	/* Force schedstat enabled if a dependent tracepoint is active */
	if (trace_sched_stat_wait_enabled()    ||
	    trace_sched_stat_sleep_enabled()   ||
	    trace_sched_stat_iowait_enabled()  ||
	    trace_sched_stat_blocked_enabled() ||
	    trace_sched_stat_runtime_enabled())
		printk_deferred_once("Scheduler tracepoints stat_sleep, stat_iowait, stat_blocked and stat_runtime require the kernel parameter schedstats=enable or kernel.sched_schedstats=1\n");
}

#else /* !CONFIG_SCHEDSTATS: */

static inline void rq_sched_info_arrive  (struct rq *rq, unsigned long long delta) { }
static inline void rq_sched_info_dequeue(struct rq *rq, unsigned long long delta) { }
static inline void rq_sched_info_depart  (struct rq *rq, unsigned long long delta) { }
# define   schedstat_enabled()		0
# define __schedstat_inc(var)		do { } while (0)
# define   schedstat_inc(var)		do { } while (0)
# define __schedstat_add(var, amt)	do { } while (0)
# define   schedstat_add(var, amt)	do { } while (0)
# define __schedstat_set(var, val)	do { } while (0)
# define   schedstat_set(var, val)	do { } while (0)
# define   schedstat_val(var)		0
# define   schedstat_val_or_zero(var)	0

# define __update_stats_wait_start(rq, p, stats)       do { } while (0)
# define __update_stats_wait_end(rq, p, stats)         do { } while (0)
# define __update_stats_enqueue_sleeper(rq, p, stats)  do { } while (0)
# define check_schedstat_required()                    do { } while (0)

#endif /* CONFIG_SCHEDSTATS */

#ifdef CONFIG_FAIR_GROUP_SCHED
struct sched_entity_stats {
	struct sched_entity     se;
	struct sched_statistics stats;
} __no_randomize_layout;
#endif

static inline struct sched_statistics *
__schedstats_from_se(struct sched_entity *se)
{
#ifdef CONFIG_FAIR_GROUP_SCHED
	if (!entity_is_task(se))
		return &container_of(se, struct sched_entity_stats, se)->stats;
#endif
	return &task_of(se)->stats;
}

static inline void psi_enqueue(struct task_struct *p, bool wakeup) {}
static inline void psi_dequeue(struct task_struct *p, bool sleep) {}
static inline void psi_ttwu_dequeue(struct task_struct *p) {}
static inline void psi_sched_switch(struct task_struct *prev,
				    struct task_struct *next,
				    bool sleep) {}

#ifdef CONFIG_SCHED_INFO
/*
 * We are interested in knowing how long it was from the *first* time a
 * task was queued to the time that it finally hit a CPU, we call this routine
 * from dequeue_task() to account for possible rq->clock skew across CPUs. The
 * delta taken on each CPU would annul the skew.
 */
static inline void sched_info_dequeue(struct rq *rq, struct task_struct *t)
{
	unsigned long long delta = 0;

	if (!t->sched_info.last_queued)
		return;

	delta = rq_clock(rq) - t->sched_info.last_queued;
	t->sched_info.last_queued = 0;
	t->sched_info.run_delay += delta;

	rq_sched_info_dequeue(rq, delta);
}

/*
 * Called when a task finally hits the CPU.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct rq *rq, struct task_struct *t)
{
	unsigned long long now, delta = 0;

	if (!t->sched_info.last_queued)
		return;

	now = rq_clock(rq);
	delta = now - t->sched_info.last_queued;
	t->sched_info.last_queued = 0;
	t->sched_info.run_delay += delta;
	t->sched_info.last_arrival = now;
	t->sched_info.pcount++;

	rq_sched_info_arrive(rq, delta);
}

/*
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeue() will clear that stamp when appropriate.
 */
static inline void sched_info_enqueue(struct rq *rq, struct task_struct *t)
{
	if (!t->sched_info.last_queued)
		t->sched_info.last_queued = rq_clock(rq);
}

/*
 * Called when a process ceases being the active-running process involuntarily
 * due, typically, to expiring its time slice (this may also be called when
 * switching to the idle task).  Now we can calculate how long we ran.
 * Also, if the process is still in the TASK_RUNNING state, call
 * sched_info_enqueue() to mark that it has now again started waiting on
 * the runqueue.
 */
static inline void sched_info_depart(struct rq *rq, struct task_struct *t)
{
	unsigned long long delta = rq_clock(rq) - t->sched_info.last_arrival;

	rq_sched_info_depart(rq, delta);

	if (task_is_running(t))
		sched_info_enqueue(rq, t);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
sched_info_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	/*
	 * prev now departs the CPU.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
	if (prev != rq->idle)
		sched_info_depart(rq, prev);

	if (next != rq->idle)
		sched_info_arrive(rq, next);
}

#else /* !CONFIG_SCHED_INFO: */
# define sched_info_enqueue(rq, t)	do { } while (0)
# define sched_info_dequeue(rq, t)	do { } while (0)
# define sched_info_switch(rq, t, next)	do { } while (0)
#endif /* CONFIG_SCHED_INFO */

#endif /* _KERNEL_STATS_H */
