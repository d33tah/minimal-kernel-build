#ifndef _LINUX_SCHED_USER_H
#define _LINUX_SCHED_USER_H

#include <linux/uidgid.h>
#include <linux/atomic.h>
#include <linux/percpu_counter.h>
#include <linux/refcount.h>
/* ratelimit_state defined in printk.h */
#include <linux/printk.h>

struct user_struct {
	refcount_t __count;
	kuid_t uid;
	struct ratelimit_state ratelimit;
};

extern struct user_struct root_user;
#define INIT_USER (&root_user)

static inline struct user_struct *get_uid(struct user_struct *u)
{
	refcount_inc(&u->__count);
	return u;
}
#endif  
