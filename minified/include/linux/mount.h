#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#include <linux/types.h>
#include <asm/barrier.h>

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
	struct super_block *mnt_sb;
	struct user_namespace *mnt_userns;
} __randomize_layout;

static inline struct user_namespace *mnt_user_ns(const struct vfsmount *mnt)
{
	 
	return smp_load_acquire(&mnt->mnt_userns);
}

extern int mnt_want_write(struct vfsmount *mnt);
extern void mnt_drop_write(struct vfsmount *mnt);
extern void mntput(struct vfsmount *mnt);
extern struct vfsmount *mntget(struct vfsmount *mnt);
extern bool mnt_may_suid(struct vfsmount *mnt);

extern int __mnt_want_write(struct vfsmount *);
extern void __mnt_drop_write(struct vfsmount *);

extern struct vfsmount *fc_mount(struct fs_context *fc);
extern struct vfsmount *vfs_create_mount(struct fs_context *fc);
extern struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				      int flags, const char *name,
				      void *data);


extern struct vfsmount *kern_mount(struct file_system_type *);

#endif  
