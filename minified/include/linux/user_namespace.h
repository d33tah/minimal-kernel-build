#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/ns_common.h>
#include <linux/uidgid.h>

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
