#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/timerqueue.h>

struct kernel_siginfo;
struct task_struct;

struct posix_cputimers { };
#define INIT_CPU_TIMERS(s)
static inline void posix_cputimers_init(struct posix_cputimers *pct) { }
static inline void posix_cputimers_group_init(struct posix_cputimers *pct,
					      u64 cpu_limit) { }

static inline void clear_posix_cputimers_work(struct task_struct *p) { }
static inline void posix_cputimers_init_work(void) { }

struct k_itimer;


#endif
