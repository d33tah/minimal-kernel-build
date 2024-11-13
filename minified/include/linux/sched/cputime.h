/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H

#include <linux/sched/signal.h>

/*
 * cputime accounting APIs:
 */


static inline bool task_cputime(struct task_struct *t,
				u64 *utime, u64 *stime)
{
	*utime = t->utime;
	*stime = t->stime;
	return false;
}

static inline u64 task_gtime(struct task_struct *t)
{
	return t->gtime;
}

static inline void task_cputime_scaled(struct task_struct *t,
				       u64 *utimescaled,
				       u64 *stimescaled)
{
	task_cputime(t, utimescaled, stimescaled);
}

extern void task_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st);
extern void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st);
extern void cputime_adjust(struct task_cputime *curr, struct prev_cputime *prev,
			   u64 *ut, u64 *st);

/*
 * Thread group CPU time accounting.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times);
void thread_group_sample_cputime(struct task_struct *tsk, u64 *samples);

/*
 * The following are functions that support scheduler-internal time accounting.
 * These functions are generally called at the timer tick.  None of this depends
 * on CONFIG_SCHEDSTATS.
 */

/**
 * get_running_cputimer - return &tsk->signal->cputimer if cputimers are active
 *
 * @tsk:	Pointer to target task.
 */
static inline
struct thread_group_cputimer *get_running_cputimer(struct task_struct *tsk)
{
	return NULL;
}

/**
 * account_group_user_time - Maintain utime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the utime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the utime field there.
 */
static inline void account_group_user_time(struct task_struct *tsk,
					   u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.utime);
}

/**
 * account_group_system_time - Maintain stime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the stime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the stime field there.
 */
static inline void account_group_system_time(struct task_struct *tsk,
					     u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.stime);
}

/**
 * account_group_exec_runtime - Maintain exec runtime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @ns:		Time value by which to increment the sum_exec_runtime field
 *		of the thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the sum_exec_runtime field there.
 */
static inline void account_group_exec_runtime(struct task_struct *tsk,
					      unsigned long long ns)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(ns, &cputimer->cputime_atomic.sum_exec_runtime);
}

static inline void prev_cputime_init(struct prev_cputime *prev)
{
	prev->utime = prev->stime = 0;
	raw_spin_lock_init(&prev->lock);
}

extern unsigned long long
task_sched_runtime(struct task_struct *task);

#endif /* _LINUX_SCHED_CPUTIME_H */
