#ifndef _LINUX_SCHED_WAKE_Q_H
#define _LINUX_SCHED_WAKE_Q_H


#include <linux/sched.h>

/*
 * The wake_q batched-wakeup API (struct wake_q_head, DEFINE_WAKE_Q,
 * wake_q_init/wake_q_empty, wake_q_add/wake_q_add_safe/wake_up_q) was removed:
 * zero users on this boot (only futex/rwsem/mutex contended-wakeup batching
 * uses it, none of which fires here).
 */

#endif
