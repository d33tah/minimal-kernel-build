#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H
#include <linux/bug.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/idr.h>
struct pid_namespace {
	struct idr idr; struct rcu_head rcu; unsigned int pid_allocated;
	struct task_struct *child_reaper; struct kmem_cache *pid_cachep;
	unsigned int level; struct pid_namespace *parent; struct user_namespace *user_ns;
	struct ucounts *ucounts; int reboot; struct ns_common ns;
} __randomize_layout;
extern struct pid_namespace init_pid_ns;
#define PIDNS_ADDING (1U << 31)
#include <linux/err.h>
static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns) { return ns; }
extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);
void pid_idr_init(void);
#endif
