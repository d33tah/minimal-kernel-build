
#ifndef _LINUX_TASKSTATS_KERN_H
#define _LINUX_TASKSTATS_KERN_H

#include <linux/sched/signal.h>
#include <linux/slab.h>

static inline void taskstats_exit(struct task_struct *tsk, int group_dead)
{}
static inline void taskstats_tgid_free(struct signal_struct *sig)
{}
static inline void taskstats_init_early(void)
{}

#endif

