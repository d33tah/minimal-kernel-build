#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/workqueue.h>
#include <linux/rwsem.h>
#include <linux/sysctl.h>

enum ucount_type {
	UCOUNT_USER_NAMESPACES,
	UCOUNT_PID_NAMESPACES,
	UCOUNT_UTS_NAMESPACES,
	UCOUNT_IPC_NAMESPACES,
	UCOUNT_NET_NAMESPACES,
	UCOUNT_MNT_NAMESPACES,
	UCOUNT_CGROUP_NAMESPACES,
	UCOUNT_TIME_NAMESPACES,
	UCOUNT_RLIMIT_NPROC,
	UCOUNT_RLIMIT_MSGQUEUE,
	UCOUNT_RLIMIT_SIGPENDING,
	UCOUNT_RLIMIT_MEMLOCK,
	UCOUNT_COUNTS,
};

#define MAX_PER_NAMESPACE_UCOUNTS UCOUNT_RLIMIT_NPROC

struct user_namespace {
	struct user_namespace	*parent;
	kuid_t			owner;
	struct ucounts		*ucounts;
	long ucount_max[UCOUNT_COUNTS];
} __randomize_layout;

struct ucounts {
	struct hlist_node node;
	struct user_namespace *ns;
	kuid_t uid;
	atomic_t count;
	atomic_long_t ucount[UCOUNT_COUNTS];
};

extern struct user_namespace init_user_ns;
extern struct ucounts init_ucounts;

struct ucounts *inc_ucount(struct user_namespace *ns, kuid_t uid, enum ucount_type type);
void dec_ucount(struct ucounts *ucounts, enum ucount_type type);
struct ucounts *alloc_ucounts(struct user_namespace *ns, kuid_t uid);
struct ucounts * __must_check get_ucounts(struct ucounts *ucounts);
void put_ucounts(struct ucounts *ucounts);

long inc_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v);
bool dec_rlimit_ucounts(struct ucounts *ucounts, enum ucount_type type, long v);

static inline void set_rlimit_ucount_max(struct user_namespace *ns,
		enum ucount_type type, unsigned long max)
{
	ns->ucount_max[type] = max <= LONG_MAX ? max : LONG_MAX;
}


static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	return &init_user_ns;
}

static inline void put_user_ns(struct user_namespace *ns)
{
}


#endif
