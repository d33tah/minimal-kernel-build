#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H


#include <linux/fsnotify_backend.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <linux/bug.h>

static inline int fsnotify_name(__u32 mask, const void *data, int data_type,
				struct inode *dir, const struct qstr *name,
				u32 cookie)
{
	if (atomic_long_read(&dir->i_sb->s_fsnotify_connectors) == 0)
		return 0;

	return fsnotify(mask, data, data_type, dir, name, NULL, cookie);
}

static inline void fsnotify_dirent(struct inode *dir, struct dentry *dentry,
				   __u32 mask)
{
	fsnotify_name(mask, dentry, FSNOTIFY_EVENT_DENTRY, dir, &dentry->d_name, 0);
}

static inline void fsnotify_inode(struct inode *inode, __u32 mask)
{
	if (atomic_long_read(&inode->i_sb->s_fsnotify_connectors) == 0)
		return;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify(mask, inode, FSNOTIFY_EVENT_INODE, NULL, NULL, inode, 0);
}

static inline int fsnotify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	struct inode *inode = d_inode(dentry);

	if (atomic_long_read(&inode->i_sb->s_fsnotify_connectors) == 0)
		return 0;

	if (S_ISDIR(inode->i_mode)) {
		mask |= FS_ISDIR;

		 
		if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
			goto notify_child;
	}

	 
	if (IS_ROOT(dentry))
		goto notify_child;

	return __fsnotify_parent(dentry, mask, data, data_type);

notify_child:
	return fsnotify(mask, data, data_type, NULL, NULL, inode, 0);
}

static inline void fsnotify_dentry(struct dentry *dentry, __u32 mask)
{
	fsnotify_parent(dentry, mask, dentry, FSNOTIFY_EVENT_DENTRY);
}

static inline int fsnotify_file(struct file *file, __u32 mask)
{
	const struct path *path = &file->f_path;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;

	return fsnotify_parent(path->dentry, mask, path, FSNOTIFY_EVENT_PATH);
}

static inline int fsnotify_perm(struct file *file, int mask)
{
	int ret;
	__u32 fsnotify_mask = 0;

	if (!(mask & (MAY_READ | MAY_OPEN)))
		return 0;

	if (mask & MAY_OPEN) {
		fsnotify_mask = FS_OPEN_PERM;

		if (file->f_flags & __FMODE_EXEC) {
			ret = fsnotify_file(file, FS_OPEN_EXEC_PERM);

			if (ret)
				return ret;
		}
	} else if (mask & MAY_READ) {
		fsnotify_mask = FS_ACCESS_PERM;
	}

	return fsnotify_file(file, fsnotify_mask);
}

/* fsnotify_link_count, fsnotify_move removed - unused */

static inline void fsnotify_inode_delete(struct inode *inode)
{
	__fsnotify_inode_delete(inode);
}

static inline void fsnotify_vfsmount_delete(struct vfsmount *mnt)
{
	__fsnotify_vfsmount_delete(mnt);
}

static inline void fsnotify_inoderemove(struct inode *inode)
{
	fsnotify_inode(inode, FS_DELETE_SELF);
	__fsnotify_inode_delete(inode);
}

static inline void fsnotify_create(struct inode *dir, struct dentry *dentry)
{
	audit_inode_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(dir, dentry, FS_CREATE);
}

/* fsnotify_link removed - unused */

static inline void fsnotify_delete(struct inode *dir, struct inode *inode,
				   struct dentry *dentry)
{
	__u32 mask = FS_DELETE;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify_name(mask, inode, FSNOTIFY_EVENT_INODE, dir, &dentry->d_name,
		      0);
}

static inline void d_delete_notify(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	ihold(inode);
	d_delete(dentry);
	fsnotify_delete(dir, inode, dentry);
	iput(inode);
}

static inline void fsnotify_unlink(struct inode *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsnotify_delete(dir, d_inode(dentry), dentry);
}

static inline void fsnotify_mkdir(struct inode *dir, struct dentry *dentry)
{
	audit_inode_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(dir, dentry, FS_CREATE | FS_ISDIR);
}

static inline void fsnotify_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsnotify_delete(dir, d_inode(dentry), dentry);
}

static inline void fsnotify_access(struct file *file)
{
	fsnotify_file(file, FS_ACCESS);
}

static inline void fsnotify_modify(struct file *file)
{
	fsnotify_file(file, FS_MODIFY);
}

static inline void fsnotify_open(struct file *file)
{
	__u32 mask = FS_OPEN;

	if (file->f_flags & __FMODE_EXEC)
		mask |= FS_OPEN_EXEC;

	fsnotify_file(file, mask);
}

static inline void fsnotify_close(struct file *file)
{
	__u32 mask = (file->f_mode & FMODE_WRITE) ? FS_CLOSE_WRITE :
						    FS_CLOSE_NOWRITE;

	fsnotify_file(file, mask);
}

/* fsnotify_xattr removed - unused */

static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid)
{
	__u32 mask = 0;

	if (ia_valid & ATTR_UID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_GID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_SIZE)
		mask |= FS_MODIFY;

	 
	if ((ia_valid & (ATTR_ATIME | ATTR_MTIME)) == (ATTR_ATIME | ATTR_MTIME))
		mask |= FS_ATTRIB;
	else if (ia_valid & ATTR_ATIME)
		mask |= FS_ACCESS;
	else if (ia_valid & ATTR_MTIME)
		mask |= FS_MODIFY;

	if (ia_valid & ATTR_MODE)
		mask |= FS_ATTRIB;

	if (mask)
		fsnotify_dentry(dentry, mask);
}

/* fsnotify_sb_error removed - unused */

#endif	 
