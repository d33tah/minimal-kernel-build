#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <linux/types.h>

/* Inlined from uapi/linux/capability.h - only v3 kept, older versions unused */
#define _LINUX_CAPABILITY_U32S_3 2

#define CAP_DAC_OVERRIDE 1
#define CAP_DAC_READ_SEARCH 2
/* End uapi/linux/capability.h */

#define _KERNEL_CAPABILITY_U32S _LINUX_CAPABILITY_U32S_3

typedef struct kernel_cap_struct {
	__u32 cap[_KERNEL_CAPABILITY_U32S];
} kernel_cap_t;

struct inode;
struct user_namespace;

#define CAP_FOR_EACH_U32(__capi) \
	for (__capi = 0; __capi < _KERNEL_CAPABILITY_U32S; ++__capi)

#define CAP_BOP_ALL(c, a, b, OP)                                        \
	do {                                                            \
		unsigned __capi;                                        \
		CAP_FOR_EACH_U32(__capi)                                \
		{                                                       \
			c.cap[__capi] = a.cap[__capi] OP b.cap[__capi]; \
		}                                                       \
	} while (0)

static inline kernel_cap_t cap_drop(const kernel_cap_t a,
				    const kernel_cap_t drop)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, drop, &~);
	return dest;
}

/* privileged_wrt_inode_uidgid moved to static in capability.c */
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap);

#endif
