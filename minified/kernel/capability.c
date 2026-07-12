
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/security.h>
#include <linux/user_namespace.h>


static bool privileged_wrt_inode_uidgid(struct user_namespace *ns, struct user_namespace *mnt_userns, const struct inode *inode)
{
	return kuid_has_mapping(ns, i_uid_into_mnt(mnt_userns, inode)) &&
	       kgid_has_mapping(ns, i_gid_into_mnt(mnt_userns, inode));
}

bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns, const struct inode *inode, int cap)
{
	struct user_namespace *ns = current_user_ns();

	return privileged_wrt_inode_uidgid(ns, mnt_userns, inode);
}
