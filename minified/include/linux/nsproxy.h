#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H
#include <linux/sched.h>
struct mnt_namespace;
struct uts_namespace;
struct pid_namespace;
struct cgroup_namespace;
struct nsproxy {
	atomic_t count;
	struct uts_namespace *uts_ns;
	struct mnt_namespace *mnt_ns;
	struct pid_namespace *pid_ns_for_children;
	struct net 	     *net_ns;
	struct time_namespace *time_ns;
	struct time_namespace *time_ns_for_children;
	struct cgroup_namespace *cgroup_ns;
};
extern struct nsproxy init_nsproxy;
int copy_namespaces(struct task_struct *tsk);
int __init nsproxy_cache_init(void);
#endif
