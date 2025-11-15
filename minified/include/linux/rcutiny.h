 
 
#ifndef __LINUX_TINY_H
#define __LINUX_TINY_H

#include <asm/param.h>  

unsigned long get_state_synchronize_rcu(void);
unsigned long start_poll_synchronize_rcu(void);
bool poll_state_synchronize_rcu(unsigned long oldstate);

static inline void cond_synchronize_rcu(unsigned long oldstate)
{
	might_sleep();
}

extern void rcu_barrier(void);

static inline void synchronize_rcu_expedited(void)
{
	synchronize_rcu();
}

 
extern void kvfree(const void *addr);

static inline void kvfree_call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	if (head) {
		call_rcu(head, func);
		return;
	}

	 
	might_sleep();
	synchronize_rcu();
	kvfree((void *) func);
}

void rcu_qs(void);

static inline void rcu_softirq_qs(void)
{
	rcu_qs();
}

#define rcu_note_context_switch(preempt) \
	do { \
		rcu_qs(); \
		rcu_tasks_qs(current, (preempt)); \
	} while (0)

static inline int rcu_needs_cpu(void)
{
	return 0;
}

 
static inline void rcu_virt_note_context_switch(int cpu) { }
static inline void rcu_cpu_stall_reset(void) { }
static inline int rcu_jiffies_till_stall_check(void) { return 21 * HZ; }
static inline void rcu_idle_enter(void) { }
static inline void rcu_idle_exit(void) { }
static inline void rcu_irq_enter(void) { }
static inline void rcu_irq_exit_irqson(void) { }
static inline void rcu_irq_enter_irqson(void) { }
static inline void rcu_irq_exit(void) { }
static inline void rcu_irq_exit_check_preempt(void) { }
#define rcu_is_idle_cpu(cpu) \
	(is_idle_task(current) && !in_nmi() && !in_hardirq() && !in_serving_softirq())
static inline void exit_rcu(void) { }
static inline bool rcu_preempt_need_deferred_qs(struct task_struct *t)
{
	return false;
}
static inline void rcu_preempt_deferred_qs(struct task_struct *t) { }
void rcu_scheduler_starting(void);
static inline void rcu_end_inkernel_boot(void) { }
static inline bool rcu_inkernel_boot_has_ended(void) { return true; }
static inline bool rcu_is_watching(void) { return true; }
static inline void rcu_momentary_dyntick_idle(void) { }
static inline void kfree_rcu_scheduler_running(void) { }
static inline bool rcu_gp_might_be_stalled(void) { return false; }

 
static inline void rcu_all_qs(void) { barrier(); }

 
#define rcutree_prepare_cpu      NULL
#define rcutree_online_cpu       NULL
#define rcutree_offline_cpu      NULL
#define rcutree_dead_cpu         NULL
#define rcutree_dying_cpu        NULL
static inline void rcu_cpu_starting(unsigned int cpu) { }

#endif  
