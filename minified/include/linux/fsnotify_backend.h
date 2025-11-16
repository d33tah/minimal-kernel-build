

#ifndef __LINUX_FSNOTIFY_BACKEND_H
#define __LINUX_FSNOTIFY_BACKEND_H

#ifdef __KERNEL__

#include <linux/types.h>


#define FS_ACCESS		0x00000001
#define FS_MODIFY		0x00000002
#define FS_ATTRIB		0x00000004
#define FS_CLOSE_WRITE		0x00000008
#define FS_CLOSE_NOWRITE	0x00000010
#define FS_OPEN			0x00000020
#define FS_MOVED_FROM		0x00000040
#define FS_MOVED_TO		0x00000080
#define FS_CREATE		0x00000100
#define FS_DELETE		0x00000200
#define FS_DELETE_SELF		0x00000400
#define FS_MOVE_SELF		0x00000800
#define FS_OPEN_EXEC		0x00001000

#define FS_UNMOUNT		0x00002000
#define FS_Q_OVERFLOW		0x00004000
#define FS_ERROR		0x00008000


#define FS_IN_IGNORED		0x00008000

#define FS_OPEN_PERM		0x00010000
#define FS_ACCESS_PERM		0x00020000
#define FS_OPEN_EXEC_PERM	0x00040000


#define FS_EVENT_ON_CHILD	0x08000000

#define FS_RENAME		0x10000000
#define FS_DN_MULTISHOT		0x20000000
#define FS_ISDIR		0x40000000

#define FS_MOVE			(FS_MOVED_FROM | FS_MOVED_TO)


enum fsnotify_data_type {
	FSNOTIFY_EVENT_NONE,
	FSNOTIFY_EVENT_PATH,
	FSNOTIFY_EVENT_INODE,
	FSNOTIFY_EVENT_DENTRY,
	FSNOTIFY_EVENT_ERROR,
};

struct inode;
struct dentry;
struct vfsmount;
struct super_block;
struct qstr;

struct fs_error_report {
	int error;
	struct inode *inode;
	struct super_block *sb;
};

static inline int fsnotify(__u32 mask, const void *data, int data_type,
			   struct inode *dir, const struct qstr *name,
			   struct inode *inode, u32 cookie)
{
	return 0;
}

static inline int __fsnotify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	return 0;
}

static inline void __fsnotify_inode_delete(struct inode *inode)
{}

static inline void __fsnotify_vfsmount_delete(struct vfsmount *mnt)
{}

static inline void fsnotify_sb_delete(struct super_block *sb)
{}

static inline void fsnotify_update_flags(struct dentry *dentry)
{}

static inline u32 fsnotify_get_cookie(void)
{
	return 0;
}

static inline void fsnotify_unmount_inodes(struct super_block *sb)
{}


#endif

#endif
