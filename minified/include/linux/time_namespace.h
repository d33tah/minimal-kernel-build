/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMENS_H
#define _LINUX_TIMENS_H


#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

struct timens_offsets {
	struct timespec64 monotonic;
	struct timespec64 boottime;
};

struct time_namespace {
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	struct ns_common	ns;
	struct timens_offsets	offsets;
	struct page		*vvar_page;
	/* If set prevents changing offsets after any task joined namespace. */
	bool			frozen_offsets;
} __randomize_layout;

extern struct time_namespace init_time_ns;

static inline int vdso_join_timens(struct task_struct *task,
				   struct time_namespace *ns)
{
	return 0;
}

static inline void timens_commit(struct task_struct *tsk,
				 struct time_namespace *ns)
{
}

static inline struct time_namespace *get_time_ns(struct time_namespace *ns)
{
	return NULL;
}

static inline void put_time_ns(struct time_namespace *ns)
{
}

static inline
struct time_namespace *copy_time_ns(unsigned long flags,
				    struct user_namespace *user_ns,
				    struct time_namespace *old_ns)
{
	if (flags & CLONE_NEWTIME)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

static inline void timens_on_fork(struct nsproxy *nsproxy,
				 struct task_struct *tsk)
{
	return;
}

static inline void timens_add_monotonic(struct timespec64 *ts) { }
static inline void timens_add_boottime(struct timespec64 *ts) { }

static inline u64 timens_add_boottime_ns(u64 nsec)
{
	return nsec;
}

static inline void timens_sub_boottime(struct timespec64 *ts) { }

static inline ktime_t timens_ktime_to_host(clockid_t clockid, ktime_t tim)
{
	return tim;
}

#endif /* _LINUX_TIMENS_H */
