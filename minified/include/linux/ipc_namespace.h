/* Minimal ipc_namespace.h - CONFIG_SYSVIPC is disabled */
#ifndef __IPC_NAMESPACE_H__
#define __IPC_NAMESPACE_H__

#include <linux/err.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>

struct user_namespace;

/* Minimal struct ipc_namespace - IPC disabled */
struct ipc_namespace {
	struct user_namespace *user_ns;
	struct ns_common ns;
};

extern struct ipc_namespace init_ipc_ns;
extern spinlock_t mq_lock;

/* shm_destroy_orphaned, mq_init_ns removed - unused */

static inline struct ipc_namespace *copy_ipcs(unsigned long flags,
	struct user_namespace *user_ns, struct ipc_namespace *ns)
{
	if (flags & CLONE_NEWIPC)
		return ERR_PTR(-EINVAL);

	return ns;
}

/* get_ipc_ns, get_ipc_ns_not_zero removed - unused */

static inline void put_ipc_ns(struct ipc_namespace *ns)
{
}

/* retire_mq_sysctls, setup_mq_sysctls removed - unused */
/* retire_ipc_sysctls, setup_ipc_sysctls removed - unused */

#endif
