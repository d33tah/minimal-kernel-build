#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H
#include <linux/sched/signal.h>
static inline bool task_cputime(struct task_struct *t, u64 *utime, u64 *stime) { *utime = t->utime; *stime = t->stime; return false; }
/* task_gtime, thread_group_cputime_adjusted, thread_group_sample_cputime - removed, not called */
/* account_group_exec_runtime call site removed */
static inline void prev_cputime_init(struct prev_cputime *prev) { prev->utime = prev->stime = 0; raw_spin_lock_init(&prev->lock); }
#endif
