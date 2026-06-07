/* Minimal fsnotify stubs */
#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H

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
