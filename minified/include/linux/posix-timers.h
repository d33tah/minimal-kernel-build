#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/timerqueue.h>

struct kernel_siginfo;
struct task_struct;

struct posix_cputimers { };
struct cpu_timer { };
#define INIT_CPU_TIMERS(s)
static inline void posix_cputimers_init(struct posix_cputimers *pct) { }
static inline void posix_cputimers_group_init(struct posix_cputimers *pct,
					      u64 cpu_limit) { }

static inline void clear_posix_cputimers_work(struct task_struct *p) { }
static inline void posix_cputimers_init_work(void) { }

#define REQUEUE_PENDING 1

struct k_itimer;

void run_posix_cpu_timers(void);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

int update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void posixtimer_rearm(struct kernel_siginfo *info);
#endif
