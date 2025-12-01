
#ifndef _INCLUDE_GUARD_LATENCYTOP_H_
#define _INCLUDE_GUARD_LATENCYTOP_H_

#include <linux/compiler.h>
struct task_struct;


static inline void
account_scheduler_latency(struct task_struct *task, int usecs, int inter)
{
}

static inline void clear_tsk_latency_tracing(struct task_struct *p)
{
}


#endif
