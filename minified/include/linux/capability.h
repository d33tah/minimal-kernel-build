#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <uapi/linux/capability.h>
#include <linux/uidgid.h>
#include <linux/stddef.h>

#define _KERNEL_CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3
#define _KERNEL_CAPABILITY_U32S    _LINUX_CAPABILITY_U32S_3

extern int file_caps_enabled;

typedef struct kernel_cap_struct {
	__u32 cap[_KERNEL_CAPABILITY_U32S];
} kernel_cap_t;

struct cpu_vfs_cap_data {
	__u32 magic_etc;
	kernel_cap_t permitted;
	kernel_cap_t inheritable;
	kuid_t rootid;
};

#define _USER_CAP_HEADER_SIZE  (sizeof(struct __user_cap_header_struct))
#define _KERNEL_CAP_T_SIZE     (sizeof(kernel_cap_t))


struct file;
struct inode;
struct dentry;
struct task_struct;
struct user_namespace;

extern const kernel_cap_t __cap_empty_set;
extern const kernel_cap_t __cap_init_eff_set;


#define CAP_FOR_EACH_U32(__capi)  \
	for (__capi = 0; __capi < _KERNEL_CAPABILITY_U32S; ++__capi)


# define CAP_FS_MASK_B0     (CAP_TO_MASK(CAP_CHOWN)		\
			    | CAP_TO_MASK(CAP_MKNOD)		\
			    | CAP_TO_MASK(CAP_DAC_OVERRIDE)	\
			    | CAP_TO_MASK(CAP_DAC_READ_SEARCH)	\
			    | CAP_TO_MASK(CAP_FOWNER)		\
			    | CAP_TO_MASK(CAP_FSETID))

# define CAP_FS_MASK_B1     (CAP_TO_MASK(CAP_MAC_OVERRIDE))

#if _KERNEL_CAPABILITY_U32S != 2
# error Fix up hand-coded capability macro initializers
#else  

#define CAP_LAST_U32			((_KERNEL_CAPABILITY_U32S) - 1)
#define CAP_LAST_U32_VALID_MASK		(CAP_TO_MASK(CAP_LAST_CAP + 1) -1)

# define CAP_EMPTY_SET    ((kernel_cap_t){{ 0, 0 }})
# define CAP_FULL_SET     ((kernel_cap_t){{ ~0, CAP_LAST_U32_VALID_MASK }})
# define CAP_FS_SET       ((kernel_cap_t){{ CAP_FS_MASK_B0 \
				    | CAP_TO_MASK(CAP_LINUX_IMMUTABLE), \
				    CAP_FS_MASK_B1 } })
# define CAP_NFSD_SET     ((kernel_cap_t){{ CAP_FS_MASK_B0 \
				    | CAP_TO_MASK(CAP_SYS_RESOURCE), \
				    CAP_FS_MASK_B1 } })

#endif  

# define cap_clear(c)         do { (c) = __cap_empty_set; } while (0)

#define cap_raise(c, flag)  ((c).cap[CAP_TO_INDEX(flag)] |= CAP_TO_MASK(flag))
#define cap_lower(c, flag)  ((c).cap[CAP_TO_INDEX(flag)] &= ~CAP_TO_MASK(flag))
#define cap_raised(c, flag) ((c).cap[CAP_TO_INDEX(flag)] & CAP_TO_MASK(flag))

#define CAP_BOP_ALL(c, a, b, OP)                                    \
do {                                                                \
	unsigned __capi;                                            \
	CAP_FOR_EACH_U32(__capi) {                                  \
		c.cap[__capi] = a.cap[__capi] OP b.cap[__capi];     \
	}                                                           \
} while (0)

#define CAP_UOP_ALL(c, a, OP)                                       \
do {                                                                \
	unsigned __capi;                                            \
	CAP_FOR_EACH_U32(__capi) {                                  \
		c.cap[__capi] = OP a.cap[__capi];                   \
	}                                                           \
} while (0)

/* cap_combine, cap_intersect removed - unused */

static inline kernel_cap_t cap_drop(const kernel_cap_t a,
				    const kernel_cap_t drop)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, drop, &~);
	return dest;
}

/* cap_invert removed - unused */

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

/* cap_drop_fs_set, cap_raise_fs_set, cap_drop_nfsd_set, cap_raise_nfsd_set removed - unused */

static inline bool has_capability(struct task_struct *t, int cap)
{
	return true;
}
static inline bool has_ns_capability(struct task_struct *t,
			      struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool has_capability_noaudit(struct task_struct *t, int cap)
{
	return true;
}
static inline bool has_ns_capability_noaudit(struct task_struct *t,
				      struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool capable(int cap)
{
	return true;
}
static inline bool ns_capable(struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool ns_capable_noaudit(struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool ns_capable_setid(struct user_namespace *ns, int cap)
{
	return true;
}
bool privileged_wrt_inode_uidgid(struct user_namespace *ns,
				 struct user_namespace *mnt_userns,
				 const struct inode *inode);
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap);
extern bool file_ns_capable(const struct file *file, struct user_namespace *ns, int cap);
extern bool ptracer_capable(struct task_struct *tsk, struct user_namespace *ns);
static inline bool perfmon_capable(void)
{
	return capable(CAP_PERFMON) || capable(CAP_SYS_ADMIN);
}

static inline bool bpf_capable(void)
{
	return capable(CAP_BPF) || capable(CAP_SYS_ADMIN);
}

static inline bool checkpoint_restore_ns_capable(struct user_namespace *ns)
{
	return ns_capable(ns, CAP_CHECKPOINT_RESTORE) ||
		ns_capable(ns, CAP_SYS_ADMIN);
}

int get_vfs_caps_from_disk(struct user_namespace *mnt_userns,
			   const struct dentry *dentry,
			   struct cpu_vfs_cap_data *cpu_caps);

int cap_convert_nscap(struct user_namespace *mnt_userns, struct dentry *dentry,
		      const void **ivalue, size_t size);

#endif  
