#ifndef _LINUX_SCHED_RT_H
#define _LINUX_SCHED_RT_H

#include <linux/sched.h>

struct task_struct;

/*
 * rt_prio/rt_task/tsk_is_pi_blocked/rt_mutex_adjust_pi/RR_TIMESLICE were all
 * removed: no RT scheduling class or PI-mutex subsystem on this build, so every
 * symbol this header defined had zero live users (tsk_is_pi_blocked()'s only
 * caller was a constant-false branch in sched_submit_work(), now folded out).
 */

#endif
