#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/stddef.h>

/* Inlined from uapi/linux/capability.h - only v3 kept, older versions unused */
/* _LINUX_CAPABILITY_VERSION_3 removed - unused */
#define _LINUX_CAPABILITY_U32S_3     2
/* __user_cap_header_struct, __user_cap_data_struct removed - userspace API never used in kernel */
/* Keep only capabilities actually used in the codebase */
#define CAP_CHOWN            0
#define CAP_DAC_OVERRIDE     1
#define CAP_DAC_READ_SEARCH  2
/* CAP_FOWNER, CAP_SYS_MODULE, CAP_SYS_CHROOT, CAP_SYS_ADMIN, CAP_SYS_RESOURCE removed - unused */
#define CAP_FSETID           4
/* CAP_MKNOD removed - never used */
#define CAP_CHECKPOINT_RESTORE	40
#define CAP_LAST_CAP         CAP_CHECKPOINT_RESTORE
#define CAP_TO_MASK(x)      (1 << ((x) & 31))

/* _KERNEL_CAPABILITY_VERSION removed - unused */
#define _KERNEL_CAPABILITY_U32S    _LINUX_CAPABILITY_U32S_3


typedef struct kernel_cap_struct {
	__u32 cap[_KERNEL_CAPABILITY_U32S];
} kernel_cap_t;


/* _USER_CAP_HEADER_SIZE, _KERNEL_CAP_T_SIZE removed - unused */


/* struct file, dentry, task_struct forward decls removed - unused */
struct inode;
struct user_namespace;

/* __cap_empty_set extern removed - never used */

#define CAP_FOR_EACH_U32(__capi)  \
	for (__capi = 0; __capi < _KERNEL_CAPABILITY_U32S; ++__capi)


/* CAP_FS_MASK_B0, CAP_FS_MASK_B1 removed - unused */

#if _KERNEL_CAPABILITY_U32S != 2
# error Fix up hand-coded capability macro initializers
#else

/* CAP_LAST_U32 removed - never used */
#define CAP_LAST_U32_VALID_MASK		(CAP_TO_MASK(CAP_LAST_CAP + 1) -1)

# define CAP_EMPTY_SET    ((kernel_cap_t){{ 0, 0 }})
# define CAP_FULL_SET     ((kernel_cap_t){{ ~0, CAP_LAST_U32_VALID_MASK }})
/* CAP_FS_SET, CAP_NFSD_SET removed - unused */
#endif

/* cap_clear macro removed - never used */

#define CAP_BOP_ALL(c, a, b, OP)                                    \
do {                                                                \
	unsigned __capi;                                            \
	CAP_FOR_EACH_U32(__capi) {                                  \
		c.cap[__capi] = a.cap[__capi] OP b.cap[__capi];     \
	}                                                           \
} while (0)
/* CAP_UOP_ALL removed - unused */

static inline kernel_cap_t cap_drop(const kernel_cap_t a,
				    const kernel_cap_t drop)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, drop, &~);
	return dest;
}

static inline bool cap_isclear(const kernel_cap_t a)
{
	unsigned __capi;
	CAP_FOR_EACH_U32(__capi) {
		if (a.cap[__capi] != 0)
			return false;
	}
	return true;
}

static inline bool cap_issubset(const kernel_cap_t a, const kernel_cap_t set)
{
	kernel_cap_t dest;
	dest = cap_drop(a, set);
	return cap_isclear(dest);
}

static inline bool capable(int cap)
{
	return true;
}
static inline bool ns_capable(struct user_namespace *ns, int cap)
{
	return true;
}

/* capable_wrt_inode_uidgid simplified - always return true for minimal kernel */
static inline bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
					    const struct inode *inode, int cap)
{
	return true;
}

/* checkpoint_restore_ns_capable removed - unused */

#endif
