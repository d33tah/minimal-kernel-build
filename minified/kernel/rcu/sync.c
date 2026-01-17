
#include <linux/rcu_sync.h>
#include <linux/sched.h>

enum { GP_IDLE = 0, GP_ENTER, GP_PASSED, GP_EXIT, GP_REPLAY };

#define rss_lock gp_wait.lock

void rcu_sync_init(struct rcu_sync *rsp)
{
	memset(rsp, 0, sizeof(*rsp));
	init_waitqueue_head(&rsp->gp_wait);
}

/* rcu_sync_func, rcu_sync_call, rcu_sync_enter removed - no external callers */

/* rcu_sync_exit removed - never called */

void rcu_sync_dtor(struct rcu_sync *rsp)
{
	int gp_state;

	WARN_ON_ONCE(READ_ONCE(rsp->gp_count));
	WARN_ON_ONCE(READ_ONCE(rsp->gp_state) == GP_PASSED);

	spin_lock_irq(&rsp->rss_lock);
	if (rsp->gp_state == GP_REPLAY)
		WRITE_ONCE(rsp->gp_state, GP_EXIT);
	gp_state = rsp->gp_state;
	spin_unlock_irq(&rsp->rss_lock);

	if (gp_state != GP_IDLE) {
		rcu_barrier();
		WARN_ON_ONCE(rsp->gp_state != GP_IDLE);
	}
}
