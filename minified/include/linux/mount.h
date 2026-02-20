#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#include <linux/types.h>
#include <asm/barrier.h>

struct super_block;
struct dentry;
struct user_namespace;
struct file_system_type;

#define MNT_NOEXEC	0x04
#define MNT_READONLY	0x40
#define MNT_WRITE_HOLD	0x200
#define MNT_INTERNAL	0x4000

#define MNT_LOCKED		0x800000
#define MNT_DOOMED		0x1000000
#define MNT_SYNC_UMOUNT		0x2000000

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

extern void mntput(struct vfsmount *mnt);
extern struct vfsmount *mntget(struct vfsmount *mnt);

extern int __mnt_want_write(struct vfsmount *);
extern void __mnt_drop_write(struct vfsmount *);

extern dev_t name_to_dev_t(const char *name);

extern struct vfsmount *kern_mount(struct file_system_type *);

#endif  
