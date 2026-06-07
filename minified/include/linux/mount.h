#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#include <linux/types.h>
#include <asm/barrier.h>

struct super_block;
struct dentry;
struct user_namespace;
struct file_system_type;
struct fs_context;
struct file;
struct path;

#define MNT_NOSUID	0x01
#define MNT_NODEV	0x02
#define MNT_NOEXEC	0x04
#define MNT_NOATIME	0x08
#define MNT_NODIRATIME	0x10
#define MNT_RELATIME	0x20
#define MNT_READONLY	0x40	 
#define MNT_NOSYMFOLLOW	0x80

#define MNT_SHRINKABLE	0x100
#define MNT_WRITE_HOLD	0x200

#define MNT_SHARED	0x1000	 
#define MNT_UNBINDABLE	0x2000	 
#define MNT_SHARED_MASK	(MNT_UNBINDABLE)
#define MNT_USER_SETTABLE_MASK  (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC \
				 | MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME \
				 | MNT_READONLY | MNT_NOSYMFOLLOW)
#define MNT_ATIME_MASK (MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME )

#define MNT_INTERNAL_FLAGS (MNT_SHARED | MNT_WRITE_HOLD | MNT_INTERNAL | \
			    MNT_DOOMED | MNT_SYNC_UMOUNT | MNT_MARKED | \
			    MNT_CURSOR)

#define MNT_INTERNAL	0x4000

#define MNT_LOCK_ATIME		0x040000
#define MNT_LOCK_NOEXEC		0x080000
#define MNT_LOCK_NOSUID		0x100000
#define MNT_LOCK_NODEV		0x200000
#define MNT_LOCK_READONLY	0x400000
#define MNT_LOCKED		0x800000
#define MNT_DOOMED		0x1000000
#define MNT_SYNC_UMOUNT		0x2000000
#define MNT_MARKED		0x4000000
#define MNT_UMOUNT		0x8000000
#define MNT_CURSOR		0x10000000

struct vfsmount {
	struct dentry *mnt_root;	 
	struct super_block *mnt_sb;	 
	int mnt_flags;
	struct user_namespace *mnt_userns;
} __randomize_layout;

static inline struct user_namespace *mnt_user_ns(const struct vfsmount *mnt)
{
	 
	return smp_load_acquire(&mnt->mnt_userns);
}

extern int mnt_want_write(struct vfsmount *mnt);
extern int mnt_want_write_file(struct file *file);
extern void mnt_drop_write(struct vfsmount *mnt);
extern void mnt_drop_write_file(struct file *file);
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


extern dev_t name_to_dev_t(const char *name);

extern struct vfsmount *kern_mount(struct file_system_type *);
extern long do_mount(const char *, const char __user *,
		     const char *, unsigned long, void *);

#endif  
