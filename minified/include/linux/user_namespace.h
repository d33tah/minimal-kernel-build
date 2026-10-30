#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/workqueue.h>
#include <linux/rwsem.h>
#include <linux/sysctl.h>

struct user_namespace {};

extern struct user_namespace init_user_ns;

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	return &init_user_ns;
}

static inline void put_user_ns(struct user_namespace *ns)
{
}

#endif
