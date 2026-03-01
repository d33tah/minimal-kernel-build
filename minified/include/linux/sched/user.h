#ifndef _LINUX_SCHED_USER_H
#define _LINUX_SCHED_USER_H

#include <linux/uidgid.h>
#include <linux/atomic.h>
#include <linux/refcount.h>

struct user_struct {
	refcount_t __count;
	kuid_t uid;
};

extern struct user_struct root_user;
#define INIT_USER (&root_user)
#endif
