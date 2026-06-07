#ifndef _LINUX_UTSNAME_H
#define _LINUX_UTSNAME_H

#include <linux/ns_common.h>

#define __NEW_UTS_LEN 64

struct new_utsname {
	char sysname[__NEW_UTS_LEN + 1];
	char nodename[__NEW_UTS_LEN + 1];
	char release[__NEW_UTS_LEN + 1];
	char version[__NEW_UTS_LEN + 1];
	char machine[__NEW_UTS_LEN + 1];
	char domainname[__NEW_UTS_LEN + 1];
};

struct uts_namespace {
	struct new_utsname name;
	struct ns_common ns;
} __randomize_layout;
extern struct uts_namespace init_uts_ns;

static inline struct new_utsname *init_utsname(void)
{
	return &init_uts_ns.name;
}

#endif  
