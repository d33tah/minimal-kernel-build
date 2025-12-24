#ifndef _LINUX_SCHED_DEADLINE_H
#define _LINUX_SCHED_DEADLINE_H
#include <linux/sched.h>
#define MAX_DL_PRIO 0
static inline int dl_prio(int prio) { return unlikely(prio < MAX_DL_PRIO); }
static inline int dl_task(struct task_struct *p) { return dl_prio(p->prio); }
#endif

