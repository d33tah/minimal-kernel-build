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
	 
	bool			frozen_offsets;
} __randomize_layout;

/* get_time_ns removed - never called */

static inline void put_time_ns(struct time_namespace *ns)
{
}

/* copy_time_ns removed - no callers */

/* timens_on_fork removed - no callers */

#endif
