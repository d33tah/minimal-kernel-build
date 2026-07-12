
#include <linux/mm.h>

int setattr_prepare(struct user_namespace *mnt_userns, struct dentry *dentry, struct iattr *attr) {
	struct inode *inode = d_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	/* ATTR_UID / ATTR_GID are never set on this build (no chown path) */


	if (ia_valid & ATTR_MODE) {
		if (!inode_owner_or_capable(mnt_userns, inode))
			return -EPERM; }

	/* ATTR_*TIME_SET / ATTR_TIMES_SET are never set on this build (utimes path removed) */

kill_priv:
	 

	return 0; }

void setattr_copy(struct user_namespace *mnt_userns, struct inode *inode, const struct iattr *attr) {
	unsigned int ia_valid = attr->ia_valid;

	/* ATTR_UID / ATTR_GID are never set on this build (no chown path) */
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		inode->i_mtime = attr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		inode->i_mode = attr->ia_mode; } }

int notify_change(struct user_namespace *mnt_userns, struct dentry *dentry, struct iattr *attr, struct inode **delegated_inode) {
	struct inode *inode = dentry->d_inode;
	umode_t mode = inode->i_mode;
	int error;
	struct timespec64 now;
	unsigned int ia_valid = attr->ia_valid;

	WARN_ON_ONCE(!inode_is_locked(inode));

	now = current_time(inode);

	attr->ia_ctime = now;
	/* ATTR_ATIME_SET / ATTR_MTIME_SET / ATTR_KILL_PRIV are never set on this build */
	attr->ia_atime = now;
	attr->ia_mtime = now;


	if ((ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID)) && (ia_valid & ATTR_MODE))
		BUG();

	if (ia_valid & ATTR_KILL_SUID) {
		if (mode & S_ISUID) {
			ia_valid = attr->ia_valid |= ATTR_MODE;
			attr->ia_mode = (inode->i_mode & ~S_ISUID); } }
	if (ia_valid & ATTR_KILL_SGID) {
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = inode->i_mode; }
			attr->ia_mode &= ~S_ISGID; } }
	if (!(attr->ia_valid & ~(ATTR_KILL_SUID | ATTR_KILL_SGID)))
		return 0;

	/* ATTR_UID / ATTR_GID are never set on this build (no chown path) */
	if (!uid_valid(i_uid_into_mnt(mnt_userns, inode)))
		return -EOVERFLOW;
	if (!gid_valid(i_gid_into_mnt(mnt_userns, inode)))
		return -EOVERFLOW;

	if (inode->i_op->setattr)
		error = inode->i_op->setattr(mnt_userns, dentry, attr);
	else
		error = simple_setattr(mnt_userns, dentry, attr);

	return error; }
