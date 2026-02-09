#include <linux/sched/signal.h>
#include <linux/capability.h>
#include <linux/security.h>
/* chgrp_ok inlined into setattr_prepare */

int setattr_prepare(struct user_namespace *mnt_userns, struct dentry *dentry,
		    struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;

	/* inode_newsize_ok always returns 0 - dead code removed */
	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	/* Inlined chown_ok */
	if (ia_valid & ATTR_UID) {
		kuid_t kuid = i_uid_into_mnt(mnt_userns, inode);
		kuid_t uid = attr->ia_uid;
		bool ok = false;
		if (uid_eq(current_fsuid(), kuid) && uid_eq(uid, inode->i_uid))
			ok = true;
		else if (capable_wrt_inode_uidgid(mnt_userns, inode, CAP_CHOWN))
			ok = true;
		else if (uid_eq(kuid, INVALID_UID) &&
			 ns_capable(inode->i_sb->s_user_ns, CAP_CHOWN))
			ok = true;
		if (!ok)
			return -EPERM;
	}

	/* Inlined chgrp_ok */
	if (ia_valid & ATTR_GID) {
		kgid_t gid = attr->ia_gid;
		kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
		bool ok = false;
		if (uid_eq(current_fsuid(),
			   i_uid_into_mnt(mnt_userns, inode))) {
			if (gid_eq(gid, inode->i_gid))
				ok = true;
			else if (in_group_p(mapped_kgid_fs(
					 mnt_userns, i_user_ns(inode), gid)))
				ok = true;
		}
		if (!ok &&
		    capable_wrt_inode_uidgid(mnt_userns, inode, CAP_CHOWN))
			ok = true;
		if (!ok && gid_eq(kgid, INVALID_GID) &&
		    ns_capable(inode->i_sb->s_user_ns, CAP_CHOWN))
			ok = true;
		if (!ok)
			return -EPERM;
	}

	if (ia_valid & ATTR_MODE) {
		kgid_t mapped_gid;

		if (!inode_owner_or_capable(mnt_userns, inode))
			return -EPERM;

		if (ia_valid & ATTR_GID)
			mapped_gid = mapped_kgid_fs(
				mnt_userns, i_user_ns(inode), attr->ia_gid);
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
	/* security_inode_killpriv always returns 0 - dead code check removed */
	return 0;
}

void setattr_copy(struct user_namespace *mnt_userns, struct inode *inode,
		  const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	/* ATTR_ATIME, ATTR_MTIME handling removed - i_atime, i_mtime never read */
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;
		kgid_t kgid = i_gid_into_mnt(mnt_userns, inode);
		if (!in_group_p(kgid) &&
		    !capable_wrt_inode_uidgid(mnt_userns, inode, CAP_FSETID))
			mode &= ~S_ISGID;
		inode->i_mode = mode;
	}
}
