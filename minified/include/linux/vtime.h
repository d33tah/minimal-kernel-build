#ifndef _LINUX_KERNEL_VTIME_H
#define _LINUX_KERNEL_VTIME_H

#include <linux/context_tracking_state.h>
#include <linux/sched.h>



static inline void vtime_init_idle(struct task_struct *tsk, int cpu) { }
/* vtime_account_irq, vtime_account_softirq, vtime_account_hardirq removed - unused */

static inline bool vtime_accounting_enabled_this_cpu(void) { return false; }
static inline void vtime_task_switch(struct task_struct *prev) { }
/* irqtime_account_irq removed - unused */

/* All vtime/irqtime functions are stubs so these are no-ops */
static inline void account_softirq_enter(struct task_struct *tsk) { }
static inline void account_softirq_exit(struct task_struct *tsk) { }
static inline void account_hardirq_enter(struct task_struct *tsk) { }
static inline void account_hardirq_exit(struct task_struct *tsk) { }

#endif  
