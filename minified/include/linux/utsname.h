#ifndef _LINUX_UTSNAME_H
#define _LINUX_UTSNAME_H


#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>

/* oldold_utsname, old_utsname removed - never used */

#define __NEW_UTS_LEN 64

struct new_utsname {
	char sysname[__NEW_UTS_LEN + 1];
	char nodename[__NEW_UTS_LEN + 1];
	char release[__NEW_UTS_LEN + 1];
	char version[__NEW_UTS_LEN + 1];
	char machine[__NEW_UTS_LEN + 1];
	char domainname[__NEW_UTS_LEN + 1];
};

/* enum uts_proc removed - unused */
struct user_namespace;
extern struct user_namespace init_user_ns;

struct uts_namespace {
	struct new_utsname name;
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct ns_common ns;
} __randomize_layout;
extern struct uts_namespace init_uts_ns;

/* copy_utsname removed - no callers */

static inline struct new_utsname *utsname(void)
{
	return &current->nsproxy->uts_ns->name;
}

static inline struct new_utsname *init_utsname(void)
{
	return &init_uts_ns.name;
}

/* uts_sem removed - never used */

#endif  
