#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H
#include <linux/sched/signal.h>
static inline bool task_cputime(struct task_struct *t, u64 *utime, u64 *stime) { *utime = t->utime; *stime = t->stime; return false; }
static inline u64 task_gtime(struct task_struct *t) { return t->gtime; }
extern void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st);
void thread_group_sample_cputime(struct task_struct *tsk, u64 *samples);
/* account_group_exec_runtime call site removed */
static inline void prev_cputime_init(struct prev_cputime *prev) { prev->utime = prev->stime = 0; raw_spin_lock_init(&prev->lock); }
#endif
