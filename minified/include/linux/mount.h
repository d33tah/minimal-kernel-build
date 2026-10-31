#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

struct super_block;
struct dentry;
struct user_namespace;
struct file_system_type;
struct fs_context;

/*
 * MNT_* flag bits removed: mnt_flags was only ever written with MNT_INTERNAL
 * / MNT_LOCKED (both never tested) so every read of any other bit was an
 * always-false branch.  Branches folded, field dropped.
 */

struct vfsmount {
	struct dentry *mnt_root;
	struct user_namespace *mnt_userns;
} __randomize_layout;

static inline struct user_namespace *mnt_user_ns(const struct vfsmount *mnt)
{
	return smp_load_acquire(&mnt->mnt_userns);
}

#endif
