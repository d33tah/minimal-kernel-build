
#include <linux/export.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/security.h>

#include <linux/xattr.h>

int setattr_prepare(struct user_namespace *mnt_userns, struct dentry *dentry,
		    struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	/* ATTR_UID / ATTR_GID are never set on this build (no chown path) */


	if (ia_valid & ATTR_MODE) {
		kgid_t mapped_gid;

		if (!inode_owner_or_capable(mnt_userns, inode))
			return -EPERM;

		mapped_gid = i_gid_into_mnt(mnt_userns, inode);


		if (!in_group_p(mapped_gid) &&
		    !capable_wrt_inode_uidgid(mnt_userns, inode, CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;
	}

	 
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) {
		if (!inode_owner_or_capable(mnt_userns, inode))
			return -EPERM;
	}

kill_priv:
	 

	return 0;
}

void setattr_copy(struct user_namespace *mnt_userns, struct inode *inode,
		  const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	/* ATTR_UID / ATTR_GID are never set on this build (no chown path) */
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		inode->i_mtime = attr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;
		kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
		if (!in_group_p(kgid) &&
		    !capable_wrt_inode_uidgid(mnt_userns, inode, CAP_FSETID))
			mode &= ~S_ISGID;
		inode->i_mode = mode;
	}
}

int notify_change(struct user_namespace *mnt_userns, struct dentry *dentry,
		  struct iattr *attr, struct inode **delegated_inode)
{
	struct inode *inode = dentry->d_inode;
	umode_t mode = inode->i_mode;
	int error;
	struct timespec64 now;
	unsigned int ia_valid = attr->ia_valid;

	WARN_ON_ONCE(!inode_is_locked(inode));

	if ((ia_valid & ATTR_MODE)) {
		umode_t amode = attr->ia_mode;
		 
		if (is_sxid(amode))
			inode->i_flags &= ~S_NOSEC;
	}

	now = current_time(inode);

	attr->ia_ctime = now;
	if (!(ia_valid & ATTR_ATIME_SET))
		attr->ia_atime = now;
	else
		attr->ia_atime = timestamp_truncate(attr->ia_atime, inode);
	if (!(ia_valid & ATTR_MTIME_SET))
		attr->ia_mtime = now;
	else
		attr->ia_mtime = timestamp_truncate(attr->ia_mtime, inode);

	if (ia_valid & ATTR_KILL_PRIV) {
		ia_valid = attr->ia_valid &= ~ATTR_KILL_PRIV;
	}

	 
	if ((ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID)) &&
	    (ia_valid & ATTR_MODE))
		BUG();

	if (ia_valid & ATTR_KILL_SUID) {
		if (mode & S_ISUID) {
			ia_valid = attr->ia_valid |= ATTR_MODE;
			attr->ia_mode = (inode->i_mode & ~S_ISUID);
		}
	}
	if (ia_valid & ATTR_KILL_SGID) {
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = inode->i_mode;
			}
			attr->ia_mode &= ~S_ISGID;
		}
	}
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

	return error;
}
