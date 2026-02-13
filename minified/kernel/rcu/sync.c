
#include <linux/wait.h>
#include <linux/rcupdate.h>
struct rcu_sync {
	int gp_state;
	wait_queue_head_t gp_wait;
};
static inline bool rcu_sync_is_idle(struct rcu_sync *rsp)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_any_held(),
			 "suspicious rcu_sync_is_idle() usage");
	return !READ_ONCE(rsp->gp_state);
}
extern void rcu_sync_init(struct rcu_sync *);
extern void rcu_sync_dtor(struct rcu_sync *);
#include <linux/sched.h>

enum { GP_IDLE = 0, GP_ENTER, GP_PASSED, GP_EXIT, GP_REPLAY };

void rcu_sync_init(struct rcu_sync *rsp)
{
	memset(rsp, 0, sizeof(*rsp));
	init_waitqueue_head(&rsp->gp_wait);
}

void rcu_sync_dtor(struct rcu_sync *rsp)
{
	WARN_ON_ONCE(rsp->gp_state != GP_IDLE);
}
