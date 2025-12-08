/* --- 2025-12-08 03:42 --- Minimal fsnotify stubs */
#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H

#include <linux/types.h>

/* Minimal defines for audit.h */
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

struct inode;
struct dentry;
struct vfsmount;
struct super_block;
struct file;

/* All fsnotify functions are stubs - fsnotify is disabled */
static inline void fsnotify_sb_delete(struct super_block *sb) {}
static inline void fsnotify_open(struct file *file) {}
static inline void fsnotify_close(struct file *file) {}
static inline void fsnotify_inoderemove(struct inode *inode) {}
static inline void fsnotify_update_flags(struct dentry *dentry) {}
static inline void fsnotify_inode_delete(struct inode *inode) {}
static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid) {}
static inline void fsnotify_create(struct inode *dir, struct dentry *dentry) {}
static inline void fsnotify_mkdir(struct inode *dir, struct dentry *dentry) {}
static inline void fsnotify_vfsmount_delete(struct vfsmount *mnt) {}
static inline void fsnotify_access(struct file *file) {}
static inline void fsnotify_modify(struct file *file) {}

#endif	/* _LINUX_FS_NOTIFY_H */
