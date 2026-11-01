#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

struct dentry;
struct user_namespace;

struct vfsmount {
	struct dentry *mnt_root;
	struct user_namespace *mnt_userns;
} __randomize_layout;

static inline struct user_namespace *mnt_user_ns(const struct vfsmount *mnt)
{
	return smp_load_acquire(&mnt->mnt_userns);
}

#endif
