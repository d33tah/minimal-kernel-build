#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H
#include <linux/spinlock.h>
#include <linux/sched.h>
struct mnt_namespace;
struct uts_namespace;
struct pid_namespace;
struct cgroup_namespace;
struct fs_struct;
struct nsproxy {
	atomic_t count;
	struct uts_namespace *uts_ns;
	/* ipc_ns removed - never used */
	struct mnt_namespace *mnt_ns;
	struct pid_namespace *pid_ns_for_children;
	struct net 	     *net_ns;
	struct time_namespace *time_ns;
	struct time_namespace *time_ns_for_children;
	struct cgroup_namespace *cgroup_ns;
};
extern struct nsproxy init_nsproxy;
/* struct nsset removed - never used */
int copy_namespaces(struct task_struct *tsk);
void exit_task_namespaces(struct task_struct *tsk);
void free_nsproxy(struct nsproxy *ns);
int __init nsproxy_cache_init(void);
static inline void put_nsproxy(struct nsproxy *ns) { if (atomic_dec_and_test(&ns->count)) free_nsproxy(ns); }
static inline void get_nsproxy(struct nsproxy *ns) { atomic_inc(&ns->count); }
#endif
