#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H

#include <linux/sched/signal.h>



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

void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times);
void thread_group_sample_cputime(struct task_struct *tsk, u64 *samples);


static inline
struct thread_group_cputimer *get_running_cputimer(struct task_struct *tsk)
{
	return NULL;
}

static inline void account_group_user_time(struct task_struct *tsk,
					   u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.utime);
}

static inline void account_group_system_time(struct task_struct *tsk,
					     u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.stime);
}

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

#endif  
