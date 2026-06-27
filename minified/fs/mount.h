
#include <linux/mount.h>
#include <linux/poll.h>
#include <linux/ns_common.h>

struct vfsmount;

struct mnt_namespace {
	struct ns_common	ns;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
} __randomize_layout;

struct mount {
	struct vfsmount mnt;
	struct mnt_namespace *mnt_ns;
} __randomize_layout;

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL)  

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

extern int __legitimize_mnt(struct vfsmount *, unsigned);
extern bool legitimize_mnt(struct vfsmount *, unsigned);

static inline void get_mnt_ns(struct mnt_namespace *ns)
{
	refcount_inc(&ns->ns.count);
}

extern seqlock_t mount_lock;
