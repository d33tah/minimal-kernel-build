/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_NOHZ_H
#define _LINUX_SCHED_NOHZ_H

/*
 * This is the interface between the scheduler and nohz/dynticks:
 */

static inline void nohz_balance_enter_idle(int cpu) { }

#ifdef CONFIG_NO_HZ_COMMON
void calc_load_nohz_start(void);
void calc_load_nohz_remote(struct rq *rq);
void calc_load_nohz_stop(void);
#else
static inline void calc_load_nohz_start(void) { }
static inline void calc_load_nohz_remote(struct rq *rq) { }
static inline void calc_load_nohz_stop(void) { }
#endif /* CONFIG_NO_HZ_COMMON */

static inline void wake_up_nohz_cpu(int cpu) { }

#endif /* _LINUX_SCHED_NOHZ_H */
