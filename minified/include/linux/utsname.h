#ifndef _LINUX_UTSNAME_H
#define _LINUX_UTSNAME_H


#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>

/* From uapi/linux/utsname.h - inlined */
#define __OLD_UTS_LEN 8

struct oldold_utsname {
	char sysname[9];
	char nodename[9];
	char release[9];
	char version[9];
	char machine[9];
};

#define __NEW_UTS_LEN 64

struct old_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
};

struct new_utsname {
	char sysname[__NEW_UTS_LEN + 1];
	char nodename[__NEW_UTS_LEN + 1];
	char release[__NEW_UTS_LEN + 1];
	char version[__NEW_UTS_LEN + 1];
	char machine[__NEW_UTS_LEN + 1];
	char domainname[__NEW_UTS_LEN + 1];
};

/* Unused enum uts_proc reduced */
enum uts_proc { UTS_PROC_UNUSED };

struct user_namespace;
extern struct user_namespace init_user_ns;

struct uts_namespace {
	struct new_utsname name;
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct ns_common ns;
} __randomize_layout;
extern struct uts_namespace init_uts_ns;

static inline void get_uts_ns(struct uts_namespace *ns)
{
}

static inline void put_uts_ns(struct uts_namespace *ns)
{
}

static inline struct uts_namespace *copy_utsname(unsigned long flags,
	struct user_namespace *user_ns, struct uts_namespace *old_ns)
{
	if (flags & CLONE_NEWUTS)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

static inline void uts_ns_init(void)
{
}

static inline void uts_proc_notify(enum uts_proc proc)
{
}

static inline struct new_utsname *utsname(void)
{
	return &current->nsproxy->uts_ns->name;
}

static inline struct new_utsname *init_utsname(void)
{
	return &init_uts_ns.name;
}

extern struct rw_semaphore uts_sem;

#endif  
