 
 

#include <linux/export.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>
#include <linux/fsnotify.h>
#include <linux/fcntl.h>
#include <linux/security.h>
#include <linux/evm.h>
#include <linux/ima.h>

 
static bool chown_ok(struct user_namespace *mnt_userns,
		     const struct inode *inode,
		     kuid_t uid)
{
	kuid_t kuid = i_uid_into_mnt(mnt_userns, inode);
	if (uid_eq(current_fsuid(), kuid) && uid_eq(uid, inode->i_uid))
		return true;
	if (capable_wrt_inode_uidgid(mnt_userns, inode, CAP_CHOWN))
		return true;
	if (uid_eq(kuid, INVALID_UID) &&
	    ns_capable(inode->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

 
static bool chgrp_ok(struct user_namespace *mnt_userns,
		     const struct inode *inode, kgid_t gid)
{
	kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
	if (uid_eq(current_fsuid(), i_uid_into_mnt(mnt_userns, inode))) {
		kgid_t mapped_gid;

		if (gid_eq(gid, inode->i_gid))
			return true;
		mapped_gid = mapped_kgid_fs(mnt_userns, i_user_ns(inode), gid);
		if (in_group_p(mapped_gid))
			return true;
	}
	if (capable_wrt_inode_uidgid(mnt_userns, inode, CAP_CHOWN))
		return true;
	if (gid_eq(kgid, INVALID_GID) &&
	    ns_capable(inode->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

 
int setattr_prepare(struct user_namespace *mnt_userns, struct dentry *dentry,
		    struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;

	 
	if (ia_valid & ATTR_SIZE) {
		int error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;
	}

	 
	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	 
	if ((ia_valid & ATTR_UID) && !chown_ok(mnt_userns, inode, attr->ia_uid))
		return -EPERM;

	 
	if ((ia_valid & ATTR_GID) && !chgrp_ok(mnt_userns, inode, attr->ia_gid))
		return -EPERM;

	 
	if (ia_valid & ATTR_MODE) {
		kgid_t mapped_gid;

		if (!inode_owner_or_capable(mnt_userns, inode))
			return -EPERM;

		if (ia_valid & ATTR_GID)
			mapped_gid = mapped_kgid_fs(mnt_userns,
						i_user_ns(inode), attr->ia_gid);
		else
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
	 
	if (ia_valid & ATTR_KILL_PRIV) {
		int error;

		error = security_inode_killpriv(mnt_userns, dentry);
		if (error)
			return error;
	}

	return 0;
}

 
/* Stub: inode_newsize_ok not used in minimal kernel */
int inode_newsize_ok(const struct inode *inode, loff_t offset) { return 0; }

 
void setattr_copy(struct user_namespace *mnt_userns, struct inode *inode,
		  const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
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

/* Stub: may_setattr not used in minimal kernel */
int may_setattr(struct user_namespace *mnt_userns, struct inode *inode,
		unsigned int ia_valid) { return 0; }

 
int notify_change(struct user_namespace *mnt_userns, struct dentry *dentry,
		  struct iattr *attr, struct inode **delegated_inode)
{
	struct inode *inode = dentry->d_inode;
	umode_t mode = inode->i_mode;
	int error;
	struct timespec64 now;
	unsigned int ia_valid = attr->ia_valid;

	WARN_ON_ONCE(!inode_is_locked(inode));

	error = may_setattr(mnt_userns, inode, ia_valid);
	if (error)
		return error;

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
		error = security_inode_need_killpriv(dentry);
		if (error < 0)
			return error;
		if (error == 0)
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

	 
	if (ia_valid & ATTR_UID &&
	    !kuid_has_mapping(inode->i_sb->s_user_ns, attr->ia_uid))
		return -EOVERFLOW;
	if (ia_valid & ATTR_GID &&
	    !kgid_has_mapping(inode->i_sb->s_user_ns, attr->ia_gid))
		return -EOVERFLOW;

	 
	if (!(ia_valid & ATTR_UID) &&
	    !uid_valid(i_uid_into_mnt(mnt_userns, inode)))
		return -EOVERFLOW;
	if (!(ia_valid & ATTR_GID) &&
	    !gid_valid(i_gid_into_mnt(mnt_userns, inode)))
		return -EOVERFLOW;

	error = security_inode_setattr(dentry, attr);
	if (error)
		return error;
	error = try_break_deleg(inode, delegated_inode);
	if (error)
		return error;

	if (inode->i_op->setattr)
		error = inode->i_op->setattr(mnt_userns, dentry, attr);
	else
		error = simple_setattr(mnt_userns, dentry, attr);

	if (!error) {
		fsnotify_change(dentry, ia_valid);
		ima_inode_post_setattr(mnt_userns, dentry);
		evm_inode_post_setattr(dentry, ia_valid);
	}

	return error;
}
