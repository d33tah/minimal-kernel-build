/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/cgroupstats.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kernfs.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
#include <linux/kernel_stat.h>

#include <linux/cgroup-defs.h>

struct kernel_clone_args;
struct cgroup_subsys_state;
struct cgroup;

static inline void cgroup_fork(struct task_struct *p) {}
static inline int cgroup_can_fork(struct task_struct *p,
				  struct kernel_clone_args *kargs) { return 0; }
static inline void cgroup_cancel_fork(struct task_struct *p,
				      struct kernel_clone_args *kargs) {}
static inline void cgroup_post_fork(struct task_struct *p,
				    struct kernel_clone_args *kargs) {}
static inline void cgroup_exit(struct task_struct *p) {}
static inline void cgroup_release(struct task_struct *p) {}
static inline void cgroup_free(struct task_struct *p) {}

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }
static inline void cgroup_init_kthreadd(void) {}
static inline void cgroup_kthread_ready(void) {}

static inline void cgroup_account_cputime(struct task_struct *task,
					  u64 delta_exec) {}
static inline void cgroup_account_cputime_field(struct task_struct *task,
						enum cpu_usage_stat index,
						u64 delta_exec) {}

struct cgroup_namespace {
	struct ns_common	ns;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	struct css_set          *root_cset;
};

extern struct cgroup_namespace init_cgroup_ns;

static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
static inline struct cgroup_namespace *
copy_cgroup_ns(unsigned long flags, struct user_namespace *user_ns,
	       struct cgroup_namespace *old_ns)
{
	return old_ns;
}

static inline void put_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns && refcount_dec_and_test(&ns->ns.count))
		free_cgroup_ns(ns);
}

static inline void cgroup_enter_frozen(void) { }
static inline void cgroup_leave_frozen(bool always_leave) { }
static inline bool cgroup_task_frozen(struct task_struct *task)
{
	return false;
}

#endif /* _LINUX_CGROUP_H */
