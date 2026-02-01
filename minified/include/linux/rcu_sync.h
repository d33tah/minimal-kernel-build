#ifndef _LINUX_RCU_SYNC_H_
#define _LINUX_RCU_SYNC_H_
#include <linux/wait.h>
#include <linux/rcupdate.h>
struct rcu_sync {
	int gp_state;
	/* gp_count removed - only used for WARN check, always 0 */
	wait_queue_head_t gp_wait;
	/* cb_head removed - rcu_sync_enter never called */
};
static inline bool rcu_sync_is_idle(struct rcu_sync *rsp)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_any_held(), "suspicious rcu_sync_is_idle() usage");
	return !READ_ONCE(rsp->gp_state);
}
extern void rcu_sync_init(struct rcu_sync *);
/* rcu_sync_enter removed - never called */
/* rcu_sync_exit removed - never called */
extern void rcu_sync_dtor(struct rcu_sync *);
/* __RCU_SYNC_INITIALIZER removed - never used */
#endif  
