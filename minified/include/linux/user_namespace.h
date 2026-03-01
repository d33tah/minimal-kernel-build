#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/uidgid.h>
#include <linux/err.h>

struct ucounts;

struct user_namespace {
	struct ns_common	ns;
} __randomize_layout;

extern struct user_namespace init_user_ns;

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	return &init_user_ns;
}

static inline void put_user_ns(struct user_namespace *ns)
{
}

#endif
