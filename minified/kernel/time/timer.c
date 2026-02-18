#include <linux/mm.h>
#include <linux/sched/debug.h>

__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

void init_timer_key(struct timer_list *timer, void (*func)(struct timer_list *),
		    unsigned int flags, const char *name,
		    struct lock_class_key *key)
{
	timer->entry.pprev = NULL;
	timer->function = func;
}

signed long __sched schedule_timeout(signed long timeout)
{
	switch (timeout) {
	case MAX_SCHEDULE_TIMEOUT:
		schedule();
		goto out;
	default:
		if (timeout < 0) {
			printk(KERN_ERR
			       "schedule_timeout: wrong timeout value %lx\n",
			       timeout);
			__set_current_state(TASK_RUNNING);
			goto out;
		}
	}

	/* For minimal Hello World, just schedule and return 0 */
	schedule();

out:
	return timeout < 0 ? 0 : timeout;
}
