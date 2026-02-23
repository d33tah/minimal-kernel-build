
#include <linux/wait.h>
#include <linux/rcupdate.h>
struct rcu_sync {
	int gp_state;
	wait_queue_head_t gp_wait;
};
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
