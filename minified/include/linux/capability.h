#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/stddef.h>

/* Inlined from uapi/linux/capability.h - only v3 kept, older versions unused */
#define _LINUX_CAPABILITY_VERSION_3  0x20080522
#define _LINUX_CAPABILITY_U32S_3     2

typedef struct __user_cap_header_struct {
	__u32 version;
	int pid;
} __user *cap_user_header_t;

typedef struct __user_cap_data_struct {
        __u32 effective;
        __u32 permitted;
        __u32 inheritable;
} __user *cap_user_data_t;


/* Keep only capabilities actually used in the codebase */
#define CAP_CHOWN            0
#define CAP_DAC_OVERRIDE     1
#define CAP_DAC_READ_SEARCH  2
#define CAP_FOWNER           3
#define CAP_FSETID           4
#define CAP_SYS_MODULE       16
#define CAP_SYS_CHROOT       18
#define CAP_SYS_ADMIN        21
#define CAP_SYS_RESOURCE     24
#define CAP_MKNOD            27
#define CAP_CHECKPOINT_RESTORE	40
#define CAP_LAST_CAP         CAP_CHECKPOINT_RESTORE
#define CAP_TO_INDEX(x)     ((x) >> 5)
#define CAP_TO_MASK(x)      (1 << ((x) & 31))

#define _KERNEL_CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3
#define _KERNEL_CAPABILITY_U32S    _LINUX_CAPABILITY_U32S_3


typedef struct kernel_cap_struct {
	__u32 cap[_KERNEL_CAPABILITY_U32S];
} kernel_cap_t;


#define _USER_CAP_HEADER_SIZE  (sizeof(struct __user_cap_header_struct))
#define _KERNEL_CAP_T_SIZE     (sizeof(kernel_cap_t))


struct file;
struct inode;
struct dentry;
struct task_struct;
struct user_namespace;

extern const kernel_cap_t __cap_empty_set;

#define CAP_FOR_EACH_U32(__capi)  \
	for (__capi = 0; __capi < _KERNEL_CAPABILITY_U32S; ++__capi)


/* CAP_FS_MASK_B0, CAP_FS_MASK_B1 removed - unused */

#if _KERNEL_CAPABILITY_U32S != 2
# error Fix up hand-coded capability macro initializers
#else

#define CAP_LAST_U32			((_KERNEL_CAPABILITY_U32S) - 1)
#define CAP_LAST_U32_VALID_MASK		(CAP_TO_MASK(CAP_LAST_CAP + 1) -1)

# define CAP_EMPTY_SET    ((kernel_cap_t){{ 0, 0 }})
# define CAP_FULL_SET     ((kernel_cap_t){{ ~0, CAP_LAST_U32_VALID_MASK }})
/* CAP_FS_SET, CAP_NFSD_SET removed - unused */
#endif

# define cap_clear(c)         do { (c) = __cap_empty_set; } while (0)

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

/* privileged_wrt_inode_uidgid moved to static in capability.c */
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap);

static inline bool checkpoint_restore_ns_capable(struct user_namespace *ns)
{
	return ns_capable(ns, CAP_CHECKPOINT_RESTORE) ||
		ns_capable(ns, CAP_SYS_ADMIN);
}

#endif
